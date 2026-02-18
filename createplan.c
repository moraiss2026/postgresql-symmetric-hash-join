/* Fait par Ablouh Mohamed Amine (300322988) et Mohammed Raiss El Fenni (300296996) */
/*
 * Ce fichier contient l'implémentation des fonctions de création de plans d'exécution 
 * dans le cadre de l'optimisation des requêtes SQL dans PostgreSQL. Les fonctions principales, 
 * comme `create_plan`, génèrent des plans basés sur le type de chemin (scan, jointure, append, etc.) 
 * en utilisant des stratégies optimales déterminées par le planificateur. Les plans de scan incluent 
 * des types tels que SeqScan, IndexScan et BitmapHeapScan, tandis que les plans de jointure prennent en 
 * charge les Hash Join, Merge Join et Nested Loop. Le fichier inclut également des fonctions auxiliaires 
 * pour construire les listes de cibles (`tlist`), gérer les contraintes d'unicité, et matérialiser des données 
 * intermédiaires. Chaque fonction est conçue pour équilibrer le coût et la taille estimés des plans afin 
 * d'optimiser l'exécution des requêtes.
 */

#include "postgres.h"
/*Inclusion principale pour les fichiers relatifs au moteur PostgreSQL.*/
#include <limits.h>

#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/plancat.h"
#include "optimizer/planmain.h"
#include "optimizer/predtest.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/tlist.h"
#include "optimizer/var.h"
#include "parser/parsetree.h"
#include "parser/parse_clause.h"
#include "parser/parse_expr.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

//Crée un plan pour les opérations de scan basées sur le chemin optimal.
static Scan *create_scan_plan(PlannerInfo *root, Path *best_path);
static List *build_relation_tlist(RelOptInfo *rel);
static bool use_physical_tlist(RelOptInfo *rel);
static void disuse_physical_tlist(Plan *plan, Path *path);
static Join *create_join_plan(PlannerInfo *root, JoinPath *best_path);
static Plan *create_append_plan(PlannerInfo *root, AppendPath *best_path);
static Result *create_result_plan(PlannerInfo *root, ResultPath *best_path);
static Material *create_material_plan(PlannerInfo *root, MaterialPath *best_path);
static Plan *create_unique_plan(PlannerInfo *root, UniquePath *best_path);
static SeqScan *create_seqscan_plan(PlannerInfo *root, Path *best_path,
					List *tlist, List *scan_clauses);
static IndexScan *create_indexscan_plan(PlannerInfo *root, IndexPath *best_path,
					  List *tlist, List *scan_clauses,
					  List **nonlossy_clauses);
static BitmapHeapScan *create_bitmap_scan_plan(PlannerInfo *root,
						BitmapHeapPath *best_path,
						List *tlist, List *scan_clauses);
static Plan *create_bitmap_subplan(PlannerInfo *root, Path *bitmapqual,
					  List **qual, List **indexqual);
static TidScan *create_tidscan_plan(PlannerInfo *root, TidPath *best_path,
					List *tlist, List *scan_clauses);
static SubqueryScan *create_subqueryscan_plan(PlannerInfo *root, Path *best_path,
						 List *tlist, List *scan_clauses);
static FunctionScan *create_functionscan_plan(PlannerInfo *root, Path *best_path,
						 List *tlist, List *scan_clauses);
static NestLoop *create_nestloop_plan(PlannerInfo *root, NestPath *best_path,
					 Plan *outer_plan, Plan *inner_plan);
static MergeJoin *create_mergejoin_plan(PlannerInfo *root, MergePath *best_path,
					  Plan *outer_plan, Plan *inner_plan);
static HashJoin *create_hashjoin_plan(PlannerInfo *root, HashPath *best_path,
					 Plan *outer_plan, Plan *inner_plan);
static void fix_indexqual_references(List *indexquals, IndexPath *index_path,
						 List **fixed_indexquals,
						 List **nonlossy_indexquals,
						 List **indexstrategy,
						 List **indexsubtype);
static Node *fix_indexqual_operand(Node *node, IndexOptInfo *index,
					  Oid *opclass);
static List *get_switched_clauses(List *clauses, Relids outerrelids);
static void copy_path_costsize(Plan *dest, Path *src);
static void copy_plan_costsize(Plan *dest, Plan *src);
static SeqScan *make_seqscan(List *qptlist, List *qpqual, Index scanrelid);
static IndexScan *make_indexscan(List *qptlist, List *qpqual, Index scanrelid,
			   Oid indexid, List *indexqual, List *indexqualorig,
			   List *indexstrategy, List *indexsubtype,
			   ScanDirection indexscandir);
static BitmapIndexScan *make_bitmap_indexscan(Index scanrelid, Oid indexid,
					  List *indexqual,
					  List *indexqualorig,
					  List *indexstrategy,
					  List *indexsubtype);
static BitmapHeapScan *make_bitmap_heapscan(List *qptlist,
					 List *qpqual,
					 Plan *lefttree,
					 List *bitmapqualorig,
					 Index scanrelid);
static TidScan *make_tidscan(List *qptlist, List *qpqual, Index scanrelid,
			 List *tideval);
static FunctionScan *make_functionscan(List *qptlist, List *qpqual,
				  Index scanrelid);
static BitmapAnd *make_bitmap_and(List *bitmapplans);
static BitmapOr *make_bitmap_or(List *bitmapplans);
static NestLoop *make_nestloop(List *tlist,
			  List *joinclauses, List *otherclauses,
			  Plan *lefttree, Plan *righttree,
			  JoinType jointype);
static HashJoin *make_hashjoin(List *tlist,
			  List *joinclauses, List *otherclauses,
			  List *hashclauses,
			  Plan *lefttree, Plan *righttree,
			  JoinType jointype);
static Hash *make_hash(Plan *lefttree);
static MergeJoin *make_mergejoin(List *tlist,
			   List *joinclauses, List *otherclauses,
			   List *mergeclauses,
			   Plan *lefttree, Plan *righttree,
			   JoinType jointype);
static Sort *make_sort(PlannerInfo *root, Plan *lefttree, int numCols,
		  AttrNumber *sortColIdx, Oid *sortOperators);
static Sort *make_sort_from_pathkeys(PlannerInfo *root, Plan *lefttree,
						List *pathkeys);

Plan *
create_plan(PlannerInfo *root, Path *best_path)
{
	Plan	   *plan;

	switch (best_path->pathtype)
	{
		case T_SeqScan:
		case T_IndexScan:
		case T_BitmapHeapScan:
		case T_TidScan:
		case T_SubqueryScan:
		case T_FunctionScan:
			plan = (Plan *) create_scan_plan(root, best_path);
			break;
		case T_HashJoin:
		case T_MergeJoin:
		case T_NestLoop:
			plan = (Plan *) create_join_plan(root,
											 (JoinPath *) best_path);
			break;
		case T_Append:
			plan = (Plan *) create_append_plan(root,
											   (AppendPath *) best_path);
			break;
		case T_Result:
			plan = (Plan *) create_result_plan(root,
											   (ResultPath *) best_path);
			break;
		case T_Material:
			plan = (Plan *) create_material_plan(root,
												 (MaterialPath *) best_path);
			break;
		case T_Unique:
			plan = (Plan *) create_unique_plan(root,
											   (UniquePath *) best_path);
			break;
		default:
			elog(ERROR, "unrecognized node type: %d",
				 (int) best_path->pathtype);
			plan = NULL;		/* keep compiler quiet */
			break;
	}

	return plan;
}

static Scan *
create_scan_plan(PlannerInfo *root, Path *best_path)
{
	RelOptInfo *rel = best_path->parent;
	List	   *tlist;
	List	   *scan_clauses;
	Scan	   *plan;

	if (use_physical_tlist(rel))
	{
		tlist = build_physical_tlist(root, rel);
		if (tlist == NIL)
			tlist = build_relation_tlist(rel);
	}
	else
		tlist = build_relation_tlist(rel);

	scan_clauses = rel->baserestrictinfo;

	switch (best_path->pathtype)
	{
		case T_SeqScan:
			plan = (Scan *) create_seqscan_plan(root,
												best_path,
												tlist,
												scan_clauses);
			break;

		case T_IndexScan:
			plan = (Scan *) create_indexscan_plan(root,
												  (IndexPath *) best_path,
												  tlist,
												  scan_clauses,
												  NULL);
			break;

		case T_BitmapHeapScan:
			plan = (Scan *) create_bitmap_scan_plan(root,
												(BitmapHeapPath *) best_path,
													tlist,
													scan_clauses);
			break;

		case T_TidScan:
			plan = (Scan *) create_tidscan_plan(root,
												(TidPath *) best_path,
												tlist,
												scan_clauses);
			break;

		case T_SubqueryScan:
			plan = (Scan *) create_subqueryscan_plan(root,
													 best_path,
													 tlist,
													 scan_clauses);
			break;

		case T_FunctionScan:
			plan = (Scan *) create_functionscan_plan(root,
													 best_path,
													 tlist,
													 scan_clauses);
			break;

		default:
			elog(ERROR, "unrecognized node type: %d",
				 (int) best_path->pathtype);
			plan = NULL;		/* keep compiler quiet */
			break;
	}

	return plan;
}

static List *
build_relation_tlist(RelOptInfo *rel)
{
	List	   *tlist = NIL;
	int			resno = 1;
	ListCell   *v;

	foreach(v, rel->reltargetlist)
	{
		Var		   *var = (Var *) copyObject(lfirst(v));

		tlist = lappend(tlist, makeTargetEntry((Expr *) var,
											   resno,
											   NULL,
											   false));
		resno++;
	}
	return tlist;
}

static bool
use_physical_tlist(RelOptInfo *rel)
{
	int			i;

	if (rel->rtekind != RTE_RELATION &&
		rel->rtekind != RTE_SUBQUERY &&
		rel->rtekind != RTE_FUNCTION)
		return false;

	if (rel->reloptkind != RELOPT_BASEREL)
		return false;

	for (i = rel->min_attr; i <= 0; i++)
	{
		if (!bms_is_empty(rel->attr_needed[i - rel->min_attr]))
			return false;
	}

	return true;
}

static void
disuse_physical_tlist(Plan *plan, Path *path)
{
	switch (path->pathtype)
	{
		case T_SeqScan:
		case T_IndexScan:
		case T_BitmapHeapScan:
		case T_TidScan:
		case T_SubqueryScan:
		case T_FunctionScan:
			plan->targetlist = build_relation_tlist(path->parent);
			break;
		default:
			break;
	}
}

static Join *
create_join_plan(PlannerInfo *root, JoinPath *best_path)
{
	Plan	   *outer_plan;
	Plan	   *inner_plan;
	Join	   *plan;

	outer_plan = create_plan(root, best_path->outerjoinpath);
	inner_plan = create_plan(root, best_path->innerjoinpath);

	switch (best_path->path.pathtype)
	{
		case T_MergeJoin:
			plan = (Join *) create_mergejoin_plan(root,
												  (MergePath *) best_path,
												  outer_plan,
												  inner_plan);
			break;
		case T_HashJoin:
			plan = (Join *) create_hashjoin_plan(root,
												 (HashPath *) best_path,
												 outer_plan,
												 inner_plan);
			break;
		case T_NestLoop:
			plan = (Join *) create_nestloop_plan(root,
												 (NestPath *) best_path,
												 outer_plan,
												 inner_plan);
			break;
		default:
			elog(ERROR, "unrecognized node type: %d",
				 (int) best_path->path.pathtype);
			plan = NULL;		/* keep compiler quiet */
			break;
	}

	return plan;
}

static Plan *
create_append_plan(PlannerInfo *root, AppendPath *best_path)
{
	Append	   *plan;
	List	   *tlist = build_relation_tlist(best_path->path.parent);
	List	   *subplans = NIL;
	ListCell   *subpaths;

	if (best_path->subpaths == NIL)
	{
		return (Plan *) make_result(tlist,
									(Node *) list_make1(makeBoolConst(false,
																	  false)),
									NULL);
	}

	foreach(subpaths, best_path->subpaths)
	{
		Path	   *subpath = (Path *) lfirst(subpaths);

		subplans = lappend(subplans, create_plan(root, subpath));
	}

	plan = make_append(subplans, false, tlist);

	return (Plan *) plan;
}

static Result *
create_result_plan(PlannerInfo *root, ResultPath *best_path)
{
	Result	   *plan;
	List	   *tlist;
	List	   *constclauses;
	Plan	   *subplan;

	if (best_path->path.parent)
		tlist = build_relation_tlist(best_path->path.parent);
	else
		tlist = NIL;

	if (best_path->subpath)
		subplan = create_plan(root, best_path->subpath);
	else
		subplan = NULL;

	constclauses = order_qual_clauses(root, best_path->constantqual);

	plan = make_result(tlist, (Node *) constclauses, subplan);

	return plan;
}

static Material *
create_material_plan(PlannerInfo *root, MaterialPath *best_path)
{
	Material   *plan;
	Plan	   *subplan;

	subplan = create_plan(root, best_path->subpath);

	disuse_physical_tlist(subplan, best_path->subpath);

	plan = make_material(subplan);

	copy_path_costsize(&plan->plan, (Path *) best_path);

	return plan;
}

static Plan *
create_unique_plan(PlannerInfo *root, UniquePath *best_path)
{
	Plan	   *plan;
	Plan	   *subplan;
	List	   *uniq_exprs;
	List	   *newtlist;
	int			nextresno;
	bool		newitems;
	int			numGroupCols;
	AttrNumber *groupColIdx;
	int			groupColPos;
	ListCell   *l;

	subplan = create_plan(root, best_path->subpath);

	if (best_path->umethod == UNIQUE_PATH_NOOP)
		return subplan;

	uniq_exprs = NIL;
	foreach(l, root->in_info_list)
	{
		InClauseInfo *ininfo = (InClauseInfo *) lfirst(l);

		if (bms_equal(ininfo->righthand, best_path->path.parent->relids))
		{
			uniq_exprs = ininfo->sub_targetlist;
			break;
		}
	}
	if (l == NULL)
		elog(ERROR, "could not find UniquePath in in_info_list");

	newtlist = build_relation_tlist(best_path->path.parent);
	nextresno = list_length(newtlist) + 1;
	newitems = false;

	foreach(l, uniq_exprs)
	{
		Node	   *uniqexpr = lfirst(l);
		TargetEntry *tle;

		tle = tlist_member(uniqexpr, newtlist);
		if (!tle)
		{
			tle = makeTargetEntry((Expr *) uniqexpr,
								  nextresno,
								  NULL,
								  false);
			newtlist = lappend(newtlist, tle);
			nextresno++;
			newitems = true;
		}
	}

	if (newitems)
	{
		if (!is_projection_capable_plan(subplan))
			subplan = (Plan *) make_result(newtlist, NULL, subplan);
		else
			subplan->targetlist = newtlist;
	}

	newtlist = subplan->targetlist;
	numGroupCols = list_length(uniq_exprs);
	groupColIdx = (AttrNumber *) palloc(numGroupCols * sizeof(AttrNumber));
	groupColPos = 0;

	foreach(l, uniq_exprs)
	{
		Node	   *uniqexpr = lfirst(l);
		TargetEntry *tle;

		tle = tlist_member(uniqexpr, newtlist);
		if (!tle)
			elog(ERROR, "failed to find unique expression in subplan tlist");
		groupColIdx[groupColPos++] = tle->resno;
	}

	if (best_path->umethod == UNIQUE_PATH_HASH)
	{
		long		numGroups;

		numGroups = (long) Min(best_path->rows, (double) LONG_MAX);

		plan = (Plan *) make_agg(root,
								 build_relation_tlist(best_path->path.parent),
								 NIL,
								 AGG_HASHED,
								 numGroupCols,
								 groupColIdx,
								 numGroups,
								 0,
								 subplan);
	}
	else
	{
		List	   *sortList = NIL;

		for (groupColPos = 0; groupColPos < numGroupCols; groupColPos++)
		{
			TargetEntry *tle;

			tle = get_tle_by_resno(subplan->targetlist,
								   groupColIdx[groupColPos]);
			Assert(tle != NULL);
			sortList = addTargetToSortList(NULL, tle,
										   sortList, subplan->targetlist,
										   SORTBY_ASC, NIL, false);
		}
		plan = (Plan *) make_sort_from_sortclauses(root, sortList, subplan);
		plan = (Plan *) make_unique(plan, sortList);
	}

	plan->plan_rows = best_path->rows;

	return plan;
}
