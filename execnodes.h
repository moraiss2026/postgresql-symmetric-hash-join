/* Fait par Ablouh Mohamed Amine (300322988) et Mohammed Raiss El Fenni (300296996) */

/* Garde-fou pour éviter les inclusions multiples */
#ifndef EXECNODES_H
#define EXECNODES_H

/* Inclusion des bibliothèques nécessaires pour les fonctionnalités de l'exécuteur */
#include "access/relscan.h"      // Gestion des scans sur les relations
#include "executor/tuptable.h"   // Gestion des tuples en mémoire
#include "fmgr.h"                // Gestion des fonctions et appels fonctionnels
#include "nodes/bitmapset.h"     // Opérations sur les ensembles de bits
#include "nodes/params.h"        // Paramètres passés aux requêtes
#include "nodes/plannodes.h"     // Définitions des nœuds de planification
#include "nodes/tidbitmap.h"     // Gestion des TID (Transaction ID) sous forme de bitmap
#include "utils/hsearch.h"       // Gestion des tables de hachage
#include "utils/tuplestore.h"    // Stockage temporaire de tuples
/* Structure pour gérer les informations d'un index */
typedef struct IndexInfo
{
	NodeTag		type;                  // Type du nœud (indique que c'est un IndexInfo)
	int			ii_NumIndexAttrs;      // Nombre d'attributs dans l'index
	AttrNumber	ii_KeyAttrNumbers[INDEX_MAX_KEYS]; // Attributs clés de l'index
	List	   *ii_Expressions;        // Expressions d'index
	List	   *ii_ExpressionsState;   // États des expressions
	List	   *ii_Predicate;          // Prédicat pour les index partiels
	List	   *ii_PredicateState;     // États des prédicats
	bool		ii_Unique;             // Indique si l'index est unique
} IndexInfo;

/* Déclaration d'une fonction callback associée au contexte d'expression */
typedef void (*ExprContextCallbackFunction) (Datum arg);

/* Structure pour gérer une pile de callbacks */
typedef struct ExprContext_CB
{
	struct ExprContext_CB *next;             // Pointeur vers le prochain callback
	ExprContextCallbackFunction function;    // Fonction callback
	Datum		arg;                          // Argument de la fonction callback
} ExprContext_CB;

/* Structure principale pour gérer les contextes d'exécution */
typedef struct ExprContext
{
	NodeTag		type;                  // Type du nœud
	TupleTableSlot *ecxt_scantuple;     // Tuple scanné actuellement
	TupleTableSlot *ecxt_innertuple;    // Tuple intérieur dans une jointure
	TupleTableSlot *ecxt_outertuple;    // Tuple extérieur dans une jointure
	MemoryContext ecxt_per_query_memory; // Mémoire allouée pour la requête
	MemoryContext ecxt_per_tuple_memory; // Mémoire allouée pour chaque tuple
	ParamExecData *ecxt_param_exec_vals; // Paramètres d'exécution
	ParamListInfo ecxt_param_list_info;  // Informations sur les paramètres de la requête
	Datum	   *ecxt_aggvalues;         // Valeurs d'agrégation
	bool	   *ecxt_aggnulls;          // Nullité des valeurs d'agrégation
	Datum		caseValue_datum;        // Valeur pour les expressions CASE
	bool		caseValue_isNull;       // Nullité pour CASE
	Datum		domainValue_datum;      // Valeur pour les domaines
	bool		domainValue_isNull;     // Nullité pour les domaines
	struct EState *ecxt_estate;         // État global d'exécution
	ExprContext_CB *ecxt_callbacks;     // Pile des callbacks
} ExprContext;

/* Résultat de l'évaluation d'une expression */
typedef enum
{
	ExprSingleResult,  // Un seul résultat est retourné
	ExprMultipleResult, // Plusieurs résultats sont retournés
	ExprEndResult      // Fin des résultats
} ExprDoneCond;

/* Modes pour les fonctions retournant des ensembles */
typedef enum
{
	SFRM_ValuePerCall = 0x01,  // Retourne une valeur par appel
	SFRM_Materialize = 0x02    // Matérialise tout l'ensemble des résultats
} SetFunctionReturnMode;

/* Informations pour le retour des ensembles de résultats */
typedef struct ReturnSetInfo
{
	NodeTag		type;                 // Type du nœud
	ExprContext *econtext;           // Contexte d'expression
	TupleDesc	expectedDesc;         // Description des tuples attendus
	int			allowedModes;         // Modes autorisés
	SetFunctionReturnMode returnMode; // Mode utilisé pour retourner les résultats
	ExprDoneCond isDone;             // État d'exécution
	Tuplestorestate *setResult;      // Stockage temporaire des résultats
	TupleDesc	setDesc;             // Description des résultats stockés
} ReturnSetInfo;


/* Gestion de la projection des colonnes dans une requête */
typedef struct ProjectionInfo
{
	NodeTag		type;               // Type du nœud
	List	   *pi_targetlist;     // Liste des colonnes à projeter
	ExprContext *pi_exprContext;   // Contexte d'expression
	TupleTableSlot *pi_slot;       // Slot pour stocker les projections
	ExprDoneCond *pi_itemIsDone;   // État d'évaluation pour chaque projection
	bool		pi_isVarList;       // Indique si la projection est une liste de variables
	int		   *pi_varSlotOffsets; // Offsets des slots dans la projection
	int		   *pi_varNumbers;     // Numéros des variables
	int			pi_lastInnerVar;    // Dernière variable intérieure utilisée
	int			pi_lastOuterVar;    // Dernière variable extérieure utilisée
	int			pi_lastScanVar;     // Dernière variable scannée
} ProjectionInfo;

/* Filtrage des colonnes inutiles */
typedef struct JunkFilter
{
	NodeTag		type;               // Type du nœud
	List	   *jf_targetList;     // Liste des colonnes à conserver
	TupleDesc	jf_cleanTupType;   // Type des tuples nettoyés
	AttrNumber *jf_cleanMap;       // Mapping des colonnes nettoyées
	TupleTableSlot *jf_resultSlot; // Slot contenant les résultats filtrés
} JunkFilter;


typedef struct ResultRelInfo
{
	NodeTag		type;
	Index		ri_RangeTableIndex;
	Relation	ri_RelationDesc;
	int			ri_NumIndices;
	RelationPtr ri_IndexRelationDescs;
	IndexInfo **ri_IndexRelationInfo;
	TriggerDesc *ri_TrigDesc;
	FmgrInfo   *ri_TrigFunctions;
	struct Instrumentation *ri_TrigInstrument;
	List	  **ri_ConstraintExprs;
	JunkFilter *ri_junkFilter;
} ResultRelInfo;

/* État global d'exécution utilisé par le plan d'exécution */
typedef struct EState
{
	NodeTag		type;                  // Type du nœud
	ScanDirection es_direction;        // Direction du scan (avant ou arrière)
	Snapshot	es_snapshot;           // Snapshot utilisé pour la lecture des tuples
	Snapshot	es_crosscheck_snapshot; // Snapshot pour vérifier les contraintes
	List	   *es_range_table;       // Table des portées pour les relations
	ResultRelInfo *es_result_relations; // Relations cibles (insert, update, delete)
	int			es_num_result_relations; // Nombre de relations cibles
	ResultRelInfo *es_result_relation_info; // Relation cible courante
	JunkFilter *es_junkFilter;        // Filtre pour les colonnes indésirables
	Relation	es_into_relation_descriptor; // Description de la relation cible (SELECT INTO)
	bool		es_into_relation_use_wal;   // Indique si WAL est utilisé
	ParamListInfo es_param_list_info;  // Informations sur les paramètres
	ParamExecData *es_param_exec_vals; // Paramètres d'exécution
	MemoryContext es_query_cxt;       // Contexte mémoire pour la requête
	TupleTable	es_tupleTable;        // Tableau des tuples alloués
	uint32		es_processed;         // Nombre de tuples traités
	Oid			es_lastoid;           // Dernier OID inséré
	List	   *es_rowMarks;         // Marquages pour les lignes verrouillées
	bool		es_forUpdate;         // Indique si c'est une requête FOR UPDATE
	bool		es_rowNoWait;         // Indique si les locks doivent attendre
	bool		es_instrument;        // Instrumentation activée
	bool		es_select_into;       // Indique si c'est un SELECT INTO
	bool		es_into_oids;         // Indique si les OIDs sont ajoutés
	List	   *es_exprcontexts;     // Liste des contextes d'expression
	ExprContext *es_per_tuple_exprcontext; // Contexte d'expression par tuple
	Plan	   *es_topPlan;          // Plan d'exécution principal
	struct evalPlanQual *es_evalPlanQual; // Plan d'évaluation pour les erreurs de validation
	bool	   *es_evTupleNull;      // Nullité des tuples validés
	HeapTuple  *es_evTuple;          // Tuples validés
	bool		es_useEvalPlan;       // Indique si le plan d'évaluation est utilisé
	TupleTableSlot *es_trig_tuple_slot; // Slot utilisé

} EState;

/* Déclaration des types pour les entrées et la table de hachage */
typedef struct TupleHashEntryData *TupleHashEntry;
typedef struct TupleHashTableData *TupleHashTable;

/* Structure représentant une entrée dans la table de hachage */
typedef struct TupleHashEntryData
{
	HeapTuple	firstTuple; // Le premier tuple dans la table
} TupleHashEntryData;

/* Structure représentant une table de hachage pour les tuples */
typedef struct TupleHashTableData
{
	HTAB	   *hashtab;         // Pointeur vers la table de hachage
	int			numCols;         // Nombre de colonnes utilisées pour le hachage
	AttrNumber *keyColIdx;       // Index des colonnes clés
	FmgrInfo   *eqfunctions;     // Fonctions de comparaison pour les colonnes clés
	FmgrInfo   *hashfunctions;   // Fonctions de hachage pour les colonnes clés
	MemoryContext tablecxt;      // Contexte mémoire pour la table
	MemoryContext tempcxt;       // Contexte mémoire temporaire
	Size		entrysize;       // Taille des entrées dans la table
	TupleTableSlot *tableslot;   // Slot pour les tuples de la table
	TupleTableSlot *inputslot;   // Slot pour les tuples d'entrée
} TupleHashTableData;


/* Définition d'un itérateur pour parcourir la table de hachage */
typedef HASH_SEQ_STATUS TupleHashIterator;

/* Macro pour réinitialiser un itérateur de la table de hachage */
#define ResetTupleHashIterator(htable, iter) \
	hash_seq_init(iter, (htable)->hashtab)

/* Macro pour scanner la table de hachage et retourner la prochaine entrée */
#define ScanTupleHashTable(iter) \
	((TupleHashEntry) hash_seq_search(iter))


/* Déclaration de la structure représentant l'état d'une expression */
typedef struct ExprState ExprState;

/* Définition de la fonction d'évaluation d'une expression */
typedef Datum (*ExprStateEvalFunc) (ExprState *expression,
									ExprContext *econtext,
									bool *isNull,
									ExprDoneCond *isDone);

/* Structure représentant l'état d'une expression */
struct ExprState
{
	NodeTag		type;         // Type du nœud
	Expr	   *expr;         // Pointeur vers l'expression
	ExprStateEvalFunc evalfunc; // Fonction d'évaluation de l'expression
};


/* État d'une expression générique */
typedef struct GenericExprState
{
	ExprState	xprstate; // État de l'expression
	ExprState  *arg;      // Argument de l'expression
} GenericExprState;

/* État pour une fonction d'agrégation */
typedef struct AggrefExprState
{
	ExprState	xprstate; // État de l'expression
	ExprState  *target;   // Cible de l'agrégation
	int			aggno;    // Numéro de l'agrégation
} AggrefExprState;

/* État pour une référence à un tableau */
typedef struct ArrayRefExprState
{
	ExprState	xprstate;      // État de l'expression
	List	   *refupperindexpr; // Expressions pour les indices supérieurs
	List	   *reflowerindexpr; // Expressions pour les indices inférieurs
	ExprState  *refexpr;      // Expression référencée
	ExprState  *refassgnexpr; // Expression pour l'affectation
	int16		refattrlength; // Longueur de l'attribut référencé
	int16		refelemlength; // Longueur des éléments du tableau
	bool		refelembyval;  // Les éléments sont-ils passés par valeur ?
	char		refelemalign;  // Alignement des éléments
} ArrayRefExprState;

/* État pour une fonction */
typedef struct FuncExprState
{
	ExprState	xprstate;       // État de l'expression
	List	   *args;           // Liste des arguments
	FmgrInfo	func;           // Informations sur la fonction
	bool		setArgsValid;   // Les arguments sont-ils valides ?
	bool		setHasSetArg;   // L'un des arguments est-il un ensemble ?
	bool		shutdown_reg;   // Fonction d'arrêt enregistrée ?
	FunctionCallInfoData setArgs; // Informations sur les arguments
} FuncExprState;

/* État pour une opération de tableau */
typedef struct ScalarArrayOpExprState
{
	FuncExprState fxprstate;   // État de l'expression fonctionnelle
	Oid			element_type;  // Type des éléments du tableau
	int16		typlen;        // Longueur des éléments
	bool		typbyval;      // Les éléments sont-ils passés par valeur ?
	char		typalign;      // Alignement des éléments
} ScalarArrayOpExprState;


typedef struct BoolExprState
{
	ExprState	xprstate;
	List	   *args;
} BoolExprState;

typedef struct SubPlanState
{
	ExprState	xprstate;
	EState	   *sub_estate;
	struct PlanState *planstate;
	List	   *exprs;
	List	   *args;
	bool		needShutdown;
	HeapTuple	curTuple;
	ProjectionInfo *projLeft;
	ProjectionInfo *projRight;
	TupleHashTable hashtable;
	TupleHashTable hashnulls;
	bool		havehashrows;
	bool		havenullrows;
	MemoryContext tablecxt;
	ExprContext *innerecontext;
	AttrNumber *keyColIdx;
	FmgrInfo   *eqfunctions;
	FmgrInfo   *hashfunctions;
} SubPlanState;

typedef struct FieldSelectState
{
	ExprState	xprstate;
	ExprState  *arg;
	TupleDesc	argdesc;
} FieldSelectState;

typedef struct FieldStoreState
{
	ExprState	xprstate;
	ExprState  *arg;
	List	   *newvals;
	TupleDesc	argdesc;
} FieldStoreState;

typedef struct ConvertRowtypeExprState
{
	ExprState	xprstate;
	ExprState  *arg;
	TupleDesc	indesc;
	TupleDesc	outdesc;
	AttrNumber *attrMap;
	Datum	   *invalues;
	bool	   *inisnull;
	Datum	   *outvalues;
	bool	   *outisnull;
} ConvertRowtypeExprState;

/* État pour une expression CASE */
typedef struct CaseExprState
{
	ExprState	xprstate;      // État de l'expression
	ExprState  *arg;          // Argument de la condition
	List	   *args;         // Liste des conditions WHEN
	ExprState  *defresult;    // Résultat par défaut
} CaseExprState;

/* État pour une clause WHEN dans une expression CASE */
typedef struct CaseWhenState
{
	ExprState	xprstate;      // État de l'expression
	ExprState  *expr;          // Condition du WHEN
	ExprState  *result;        // Résultat si la condition est vraie
} CaseWhenState;

/* État pour une expression de tableau */
typedef struct ArrayExprState
{
	ExprState	xprstate;      // État de l'expression
	List	   *elements;      // Liste des éléments du tableau
	int16		elemlength;    // Longueur des éléments
	bool		elembyval;     // Les éléments sont-ils passés par valeur ?
	char		elemalign;     // Alignement des éléments
} ArrayExprState;

/* État pour une expression Row */
typedef struct RowExprState
{
	ExprState	xprstate;      // État de l'expression
	List	   *args;          // Arguments de la ligne
	TupleDesc	tupdesc;        // Description du tuple
} RowExprState;


/* State structure for COALESCE expressions */
typedef struct CoalesceExprState
{
	ExprState	xprstate; // Base expression state
	List	   *args;     // List of arguments to evaluate
} CoalesceExprState;

/* State structure for MIN/MAX expressions */
typedef struct MinMaxExprState
{
	ExprState	xprstate; // Base expression state
	List	   *args;     // List of arguments
	FmgrInfo	cfunc;    // Function manager info for comparison
} MinMaxExprState;


/* State structure for coercion to a domain type */
typedef struct CoerceToDomainState
{
	ExprState	xprstate;      // Base expression state
	ExprState  *arg;          // Argument expression
	List	   *constraints;  // List of constraints to check
} CoerceToDomainState;

/* Enum defining the types of domain constraints */
typedef enum DomainConstraintType
{
	DOM_CONSTRAINT_NOTNULL, // NOT NULL constraint
	DOM_CONSTRAINT_CHECK    // CHECK constraint
} DomainConstraintType;

/* State structure for a single domain constraint */
typedef struct DomainConstraintState
{
	NodeTag		type;             // Node type
	DomainConstraintType constrainttype; // Type of domain constraint
	char	   *name;             // Name of the constraint
	ExprState  *check_expr;       // Expression to evaluate the constraint
} DomainConstraintState;

/* Base structure for maintaining the state of a plan node */
typedef struct PlanState
{
	NodeTag		type;             // Type of node
	Plan	   *plan;             // Reference to the associated plan node
	EState	   *state;            // Executor state
	struct Instrumentation *instrument; // Instrumentation data (optional)
	List	   *targetlist;       // Target list for projection
	List	   *qual;             // Qualification (filter condition)
	struct PlanState *lefttree;   // Left child node
	struct PlanState *righttree;  // Right child node
	List	   *initPlan;         // List of init plans
	List	   *subPlan;          // List of subplans
	Bitmapset  *chgParam;         // Changed parameters
	TupleTableSlot *ps_OuterTupleSlot; // Outer tuple slot
	TupleTableSlot *ps_InnerTupleSlot; // Inner tuple slot
	TupleTableSlot *ps_ResultTupleSlot; // Result tuple slot
	ExprContext *ps_ExprContext;  // Expression context
	ProjectionInfo *ps_ProjInfo;  // Projection information
	bool		ps_TupFromTlist;  // Are tuples fetched from the target list?
} PlanState;

/* Macros for accessing child nodes of a PlanState */
#define innerPlanState(node)		(((PlanState *)(node))->righttree) // Right child
#define outerPlanState(node)		(((PlanState *)(node))->lefttree)  // Left child

/* State for a Result plan node */
typedef struct ResultState
{
	PlanState	ps;                 // Base plan state
	ExprState  *resconstantqual;    // Constant qualification condition
	bool		rs_done;            // Is the result complete?
	bool		rs_checkqual;       // Should qualifications be checked?
} ResultState;

/* State for an Append plan node */
typedef struct AppendState
{
	PlanState	ps;                // Base plan state
	PlanState **appendplans;      // Array of child plan states
	int			as_nplans;         // Number of child plans
	int			as_whichplan;      // Index of the current plan
	int			as_firstplan;      // First plan index
	int			as_lastplan;       // Last plan index
} AppendState;

/* State for a Bitmap AND plan node */
typedef struct BitmapAndState
{
	PlanState	ps;               // Base plan state
	PlanState **bitmapplans;     // Array of child bitmap plan states
	int			nplans;           // Number of child plans
} BitmapAndState;

/* State for a Bitmap OR plan node */
typedef struct BitmapOrState
{
	PlanState	ps;               // Base plan state
	PlanState **bitmapplans;     // Array of child bitmap plan states
	int			nplans;           // Number of child plans
} BitmapOrState;


/* Base state for scan nodes */
typedef struct ScanState
{
	PlanState	ps;                  // Base plan state
	Relation	ss_currentRelation;  // Current relation being scanned
	HeapScanDesc ss_currentScanDesc; // Descriptor for heap scans
	TupleTableSlot *ss_ScanTupleSlot; // Slot for scanned tuples
} ScanState;

/* Sequential scan state */
typedef ScanState SeqScanState;

/* State for index scan nodes */
typedef struct IndexScanState
{
	ScanState	ss;                     // Base scan state
	List	   *indexqualorig;         // Original index qualifications
	ScanKey		iss_ScanKeys;          // Index scan keys
	int			iss_NumScanKeys;       // Number of scan keys
	ExprState **iss_RuntimeKeyInfo;    // Runtime key information
	ExprContext *iss_RuntimeContext;   // Runtime context
	bool		iss_RuntimeKeysReady;  // Are runtime keys ready?
	Relation	iss_RelationDesc;      // Index relation descriptor
	IndexScanDesc iss_ScanDesc;        // Index scan descriptor
} IndexScanState;


typedef struct BitmapIndexScanState
{
	ScanState	ss;
	TIDBitmap  *biss_result;
	ScanKey		biss_ScanKeys;
	int			biss_NumScanKeys;
	ExprState **biss_RuntimeKeyInfo;
	ExprContext *biss_RuntimeContext;
	bool		biss_RuntimeKeysReady;
	Relation	biss_RelationDesc;
	IndexScanDesc biss_ScanDesc;
} BitmapIndexScanState;

typedef struct BitmapHeapScanState
{
	ScanState	ss;
	List	   *bitmapqualorig;
	TIDBitmap  *tbm;
	TBMIterateResult *tbmres;
	int			curslot;
	int			minslot;
	int			maxslot;
} BitmapHeapScanState;

typedef struct TidScanState
{
	ScanState	ss;
	List	   *tss_tideval;
	int			tss_NumTids;
	int			tss_TidPtr;
	int			tss_MarkTidPtr;
	ItemPointerData *tss_TidList;
	HeapTupleData tss_htup;
} TidScanState;

typedef struct SubqueryScanState
{
	ScanState	ss;
	PlanState  *subplan;
	EState	   *sss_SubEState;
} SubqueryScanState;

typedef struct FunctionScanState
{
	ScanState	ss;
	TupleDesc	tupdesc;
	Tuplestorestate *tuplestorestate;
	ExprState  *funcexpr;
} FunctionScanState;

/* State for a Merge Join plan node */
typedef struct MergeJoinState
{
	JoinState	js;                      // Base join state
	int			mj_NumClauses;          // Number of join clauses
	MergeJoinClause mj_Clauses;          // Join clauses
	int			mj_JoinState;           // Current state of the join
	bool		mj_FillOuter;           // Fill unmatched outer tuples?
	bool		mj_FillInner;           // Fill unmatched inner tuples?
	bool		mj_MatchedOuter;        // Was the outer tuple matched?
	bool		mj_MatchedInner;        // Was the inner tuple matched?
	TupleTableSlot *mj_OuterTupleSlot;   // Outer tuple slot
	TupleTableSlot *mj_InnerTupleSlot;   // Inner tuple slot
	TupleTableSlot *mj_MarkedTupleSlot;  // Marked tuple slot
	TupleTableSlot *mj_NullOuterTupleSlot; // Null outer tuple slot
	TupleTableSlot *mj_NullInnerTupleSlot; // Null inner tuple slot
	ExprContext *mj_OuterEContext;       // Outer expression context
	ExprContext *mj_InnerEContext;       // Inner expression context
} MergeJoinState;

/* State for a Hash Join plan node */
typedef struct HashJoinState
{
	JoinState	js;                      // Base join state
	List	   *hashclauses;            // Hash join clauses
	HashJoinTable hj_InnerHashTable;     // Inner hash table
	HashJoinTable hj_OuterHashTable;     // Outer hash table
	uint32		hj_InnerCurHashValue;    // Current hash value for the inner table
	uint32		hj_OuterCurHashValue;    // Current hash value for the outer table
	int			hj_InnerCurBucketNo;     // Current bucket number for the inner table
	int			hj_OuterCurBucketNo;     // Current bucket number for the outer table
	HashJoinTuple hj_InnerCurTuple;      // Current tuple for the inner table
	HashJoinTuple hj_OuterCurTuple;      // Current tuple for the outer table
	List	   *hj_InnerHashKeys;        // Hash keys for the inner table
	List	   *hj_OuterHashKeys;        // Hash keys for the outer table
	List	   *hj_HashOperators;        // Hash operators
	TupleTableSlot *hj_InnerTupleSlot;   // Inner tuple slot
	TupleTableSlot *hj_OuterTupleSlot;   // Outer tuple slot
	TupleTableSlot *hj_InnerHashTupleSlot; // Inner hash tuple slot
	TupleTableSlot *hj_OuterHashTupleSlot; // Outer hash tuple slot
	TupleTableSlot *hj_NullInnerTupleSlot; // Null inner tuple slot
	TupleTableSlot *hj_FirstInnerTupleSlot; // First inner tuple slot
	TupleTableSlot *hj_FirstOuterTupleSlot; // First outer tuple slot
	bool        hj_innerExhausted;       // Is the inner table exhausted?
	bool        hj_outerExhausted;       // Is the outer table exhausted?
	bool        hj_NeedNewInner;         // Does the join need a new inner tuple?
	bool        hj_NeedNewOuter;         // Does the join need a new outer tuple?
	bool        hj_MatchedOuter;         // Was the outer tuple matched?
	bool        hj_InnerNotEmpty;        // Is the inner table not empty?
	bool        hj_OuterNotEmpty;        // Is the outer table not empty?
	bool        hj_fetchingFromInner;    // Is the join fetching from the inner table?
	int         hj_foundByProbingInner;  // Tuples found by probing the inner table
	int         hj_foundByProbingOuter;  // Tuples found by probing the outer table
} HashJoinState;
#endif   /* EXECNODES_H */
