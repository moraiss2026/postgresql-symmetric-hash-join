/* Fait par Ablouh Mohamed Amine (300322988) et Mohammed Raiss El Fenni (300296996) */

#include "postgres.h"

#include "executor/executor.h"
#include "executor/hashjoin.h"
#include "executor/nodeHash.h"
#include "executor/nodeHashjoin.h"
#include "optimizer/clauses.h"
#include "utils/memutils.h"

/* On déclare les fonctions statiques utilisées pour le Symmetric Hash Join */
static TupleTableSlot *ExecHashJoinOuterGetTuple(PlanState *outerNode,
						  HashJoinState *hjstate,
						  uint32 *hashvalue);
static TupleTableSlot *ExecHashJoinGetSavedTuple(HashJoinState *hjstate,
						  BufFile *file,
						  uint32 *hashvalue,
						  TupleTableSlot *tupleSlot);
static int	ExecHashJoinNewBatch(HashJoinState *hjstate);

/* Fonction principale d'exécution du Symmetric Hash Join */
TupleTableSlot * ExecHashJoin(HashJoinState *node)
{
	EState	   *estate;
	HashState  *outerHashNode;
	HashState  *innerHashNode;
	List	   *joinqual;
	List	   *otherqual;

	TupleTableSlot *inntuple;
	TupleTableSlot *outtuple;
	ExprContext *econtext;
	ExprDoneCond isDone;
	HashJoinTable innerHashtable;
	HashJoinTable outerHashtable;
	
	HeapTuple	curtuple;
	TupleTableSlot *innerTupleSlot;
	TupleTableSlot *outerTupleSlot;
	uint32	hashvalue;
	int batchno;

	estate = node->js.ps.state;
	joinqual = node->js.joinqual;
	otherqual = node->js.ps.qual;
	innerHashNode = (HashState *) innerPlanState(node);
	outerHashNode = (HashState *) outerPlanState(node);

	innerHashtable = node->hj_InnerHashTable;
	outerHashtable = node->hj_OuterHashTable;
	econtext = node->js.ps.ps_ExprContext;

	ResetExprContext(econtext);

	if (innerHashtable == NULL && outerHashtable == NULL) 
	{
		innerHashtable = ExecHashTableCreate((Hash *) innerHashNode->ps.plan, node->hj_HashOperators);
		node->hj_InnerHashTable = innerHashtable;

        outerHashtable = ExecHashTableCreate((Hash *) outerHashNode->ps.plan, node->hj_HashOperators); 
        node->hj_OuterHashTable = outerHashtable;

		innerHashNode->hashtable = innerHashtable;
		outerHashNode->hashtable = outerHashtable;
	}
	/* Boucle principale pour traiter les tuples */
for (;;) {
    /* On vérifie si l'une des relations est encore disponible */
    if (!node->hj_innerExhausted || !node->hj_outerExhausted) {
        /* Si la relation intérieure est épuisée, on bascule vers la relation extérieure */
        if (node->hj_innerExhausted) {
            node->hj_fetchingFromInner = false;
        }
        /* Si la relation extérieure est épuisée, on revient à la relation intérieure */
        if (node->hj_outerExhausted) {
            node->hj_fetchingFromInner = true;
        }
        /* Gestion des tuples de la relation intérieure */
        if (!node->hj_innerExhausted && node->hj_NeedNewInner && node->hj_fetchingFromInner) {
            /* On récupère le prochain tuple de la relation intérieure */
            innerTupleSlot = ExecProcNode((PlanState *) innerHashNode);
            node->js.ps.ps_InnerTupleSlot = innerTupleSlot;

            if (!TupIsNull(innerTupleSlot)) {
                node->hj_NeedNewInner = false;

                /* Mise à jour du contexte d'expression avec le tuple intérieur */
                econtext->ecxt_innertuple = innerTupleSlot;

                /* Calcul de la valeur de hachage pour le tuple intérieur */
                hashvalue = ExecHashGetHashValue(outerHashtable, econtext, node->hj_InnerHashKeys);

                /* Mise à jour des informations de hachage et de position */
                node->hj_InnerCurHashValue = hashvalue;
                ExecHashGetBucketAndBatch(outerHashtable, hashvalue, &node->hj_OuterCurBucketNo, &batchno);
                node->hj_OuterCurTuple = NULL;
            } else {
                /* Si aucun tuple n'est trouvé, on marque la relation intérieure comme épuisée */
                node->hj_innerExhausted = true;
            }
        }

        /* Gestion des tuples de la relation extérieure */
        if (!node->hj_outerExhausted && node->hj_NeedNewOuter && !node->hj_fetchingFromInner) {
            /* On récupère le prochain tuple de la relation extérieure */
            outerTupleSlot = ExecProcNode((PlanState *) outerHashNode);
            node->js.ps.ps_OuterTupleSlot = outerTupleSlot;

            if (!TupIsNull(outerTupleSlot)) {
                node->hj_NeedNewOuter = false;

                /* Mise à jour du contexte d'expression avec le tuple extérieur */
                econtext->ecxt_outertuple = outerTupleSlot;

                /* Calcul de la valeur de hachage pour le tuple extérieur */
                hashvalue = ExecHashGetHashValue(innerHashtable, econtext, node->hj_OuterHashKeys);

                /* Mise à jour des informations de hachage et de position */
                node->hj_OuterCurHashValue = hashvalue;
                ExecHashGetBucketAndBatch(innerHashtable, hashvalue, &node->hj_InnerCurBucketNo, &batchno);
                node->hj_InnerCurTuple = NULL;
            } else {
                /* Si aucun tuple n'est trouvé, on marque la relation extérieure comme épuisée */
                node->hj_outerExhausted = true;
            }
        }
        /* Si les deux relations sont épuisées, on termine la boucle */
        if (node->hj_innerExhausted && node->hj_outerExhausted) {
            return NULL;
        }

        /* Traitement des tuples restants dans la relation intérieure */
        if (!TupIsNull(node->js.ps.ps_InnerTupleSlot) && node->hj_fetchingFromInner) {
            for (;;) {
                econtext->ecxt_innertuple = node->js.ps.ps_InnerTupleSlot;
                curtuple = ExecScanHashBucket(node, econtext);

                if (curtuple == NULL) {
                    node->hj_fetchingFromInner = false; // Plus de tuples à traiter
                    break;
                }

                /* On évalue le tuple et le stocke */
                outtuple = ExecStoreTuple(curtuple, node->hj_OuterTupleSlot, InvalidBuffer, false);
                econtext->ecxt_outertuple = outtuple;

                ResetExprContext(econtext);

                /* Évaluation des conditions de jointure */
                if (joinqual == NIL || ExecQual(joinqual, econtext, false)) {
                    if (otherqual == NIL || ExecQual(otherqual, econtext, false)) {
                        TupleTableSlot *result = ExecProject(node->js.ps.ps_ProjInfo, &isDone);
                        if (isDone != ExprEndResult) {
                            node->hj_foundByProbingOuter++;
                            node->js.ps.ps_TupFromTlist = (isDone == ExprMultipleResult);
                            return result;
                        }
                    }
                }
            }
            node->hj_NeedNewInner = true;
            node->js.ps.ps_InnerTupleSlot = NULL;
            node->hj_fetchingFromInner = false;
            continue;
        }
    }
}

	return NULL;
}
HashJoinState * ExecInitHashJoin(HashJoin *node, EState *estate)
{
    /* On alloue la structure d'état pour le Hash Join */
	HashJoinState *hjstate;

    /* Déclaration des variables pour les nœuds Hash des relations intérieure et extérieure */
    Hash       *outerHashNode;
    Hash       *innerHashNode;

    /* Listes pour les clauses de jointure et les opérateurs */
	List	   *lclauses;
	List	   *rclauses;
	List	   *hoperators;
	ListCell   *l;

    /* On crée un nouveau nœud Hash Join */
	hjstate = makeNode(HashJoinState);

    /* On initialise le plan et l'état pour ce nœud */
	hjstate->js.ps.plan = (Plan *) node;
	hjstate->js.ps.state = estate;

    /* On assigne un contexte d'expression à ce nœud */
	ExecAssignExprContext(estate, &hjstate->js.ps);

    /* On initialise les listes d'expressions pour la cible et les qualifications */
	hjstate->js.ps.targetlist = (List *)
		ExecInitExpr((Expr *) node->join.plan.targetlist,
					 (PlanState *) hjstate);

	hjstate->js.ps.qual = (List *)
		ExecInitExpr((Expr *) node->join.plan.qual,
					 (PlanState *) hjstate);

    /* On configure le type de jointure (INNER, LEFT, etc.) */
	hjstate->js.jointype = node->join.jointype;

    /* On initialise les conditions de jointure */
	hjstate->js.joinqual = (List *)
		ExecInitExpr((Expr *) node->join.joinqual,
					 (PlanState *) hjstate);

    /* On initialise les clauses spécifiques au Hash Join */
	hjstate->hashclauses = (List *)
		ExecInitExpr((Expr *) node->hashclauses,
					 (PlanState *) hjstate);

    /* On récupère les nœuds Hash des relations intérieure et extérieure */
    outerHashNode = (Hash *) outerPlan(node);
    innerHashNode = (Hash *) innerPlan(node);

    /* On initialise les états des nœuds enfants (Hash pour chaque relation) */
    outerPlanState(hjstate) = ExecInitNode((Plan *) outerHashNode, estate);
    innerPlanState(hjstate) = ExecInitNode((Plan *) innerHashNode, estate);

    /* On définit le nombre de slots nécessaires pour ce Hash Join */
#define HASHJOIN_NSLOTS 3

	ExecInitResultTupleSlot(estate, &hjstate->js.ps);
	hjstate->hj_OuterTupleSlot = ExecInitExtraTupleSlot(estate);
	hjstate->hj_InnerTupleSlot = ExecInitExtraTupleSlot(estate);	

    /* On traite les différents types de jointure possibles */
	switch (node->join.jointype)
	{
		case JOIN_INNER:
		case JOIN_IN:
			break;
		case JOIN_LEFT:
            /* Pour une jointure gauche, on prépare un slot avec des tuples NULL pour la relation intérieure */
			hjstate->hj_NullInnerTupleSlot =
			ExecInitNullTupleSlot(estate,
			ExecGetResultType(innerPlanState(hjstate)));
			break;
		default:
            /* On génère une erreur si le type de jointure n'est pas reconnu */
			elog(ERROR, "unrecognized join type: %d",
			(int) node->join.jointype);
	}
    {
        /* On récupère le slot contenant les tuples hachés pour la relation intérieure */
        HashState  *hashstate = (HashState *) innerPlanState(hjstate);
        TupleTableSlot *slot = hashstate->ps.ps_ResultTupleSlot;
        hjstate->hj_InnerHashTupleSlot = slot;

        /* De manière similaire, on récupère le slot pour la relation extérieure */
        hashstate = (HashState *) outerPlanState(hjstate); 
        slot = hashstate->ps.ps_ResultTupleSlot; 
        hjstate->hj_OuterHashTupleSlot = slot; 
    }

    /* On configure le type de tuple attendu en sortie du Hash Join */
    ExecAssignResultTypeFromTL(&hjstate->js.ps);
    ExecAssignProjectionInfo(&hjstate->js.ps);

    /* On définit les descripteurs de slots pour les relations intérieure et extérieure */
    ExecSetSlotDescriptor(hjstate->hj_OuterTupleSlot,
                          ExecGetResultType(outerPlanState(hjstate)),
                          false);

    ExecSetSlotDescriptor(hjstate->hj_InnerTupleSlot, 
                          ExecGetResultType(innerPlanState(hjstate)),
                          false); 

    /* Initialisation des structures spécifiques au Hash Join */
    hjstate->hj_InnerHashTable = NULL;
    hjstate->hj_FirstOuterTupleSlot = NULL;
    hjstate->hj_OuterHashTable = NULL;
    hjstate->hj_FirstInnerTupleSlot = NULL;

    /* On initialise les valeurs par défaut pour le traitement */
    hjstate->hj_InnerCurHashValue = 0;
    hjstate->hj_InnerCurBucketNo = 0;
    hjstate->hj_InnerCurTuple = NULL;

    hjstate->hj_OuterCurHashValue = 0;
    hjstate->hj_OuterCurBucketNo = 0;
    hjstate->hj_OuterCurTuple = NULL; 
    
    /* On récupère et configure les clés de hachage pour chaque relation */
    lclauses = NIL;
    rclauses = NIL;
    hoperators = NIL;

    foreach(l, hjstate->hashclauses)
    {
        FuncExprState *fstate = (FuncExprState *) lfirst(l);
        OpExpr	   *hclause;

        /* Chaque clause doit être une opération binaire */
        Assert(IsA(fstate, FuncExprState));
        hclause = (OpExpr *) fstate->xprstate.expr;
        Assert(IsA(hclause, OpExpr));

        /* On sépare les clés de hachage en deux listes (intérieure et extérieure) */
        lclauses = lappend(lclauses, linitial(fstate->args));
        rclauses = lappend(rclauses, lsecond(fstate->args));
        hoperators = lappend_oid(hoperators, hclause->opno);
    }

    /* On assigne les clés de hachage à l'état du Hash Join */
    hjstate->hj_OuterHashKeys = lclauses;
    hjstate->hj_InnerHashKeys = rclauses;
    hjstate->hj_HashOperators = hoperators;

    /* On configure les clés de hachage pour les nœuds Hash */
    ((HashState *) innerPlanState(hjstate))->hashkeys = rclauses;
    ((HashState *) outerPlanState(hjstate))->hashkeys = lclauses; 

    /* Initialisation des slots et indicateurs */
    hjstate->js.ps.ps_OuterTupleSlot = NULL;
    hjstate->js.ps.ps_InnerTupleSlot = NULL;
    hjstate->js.ps.ps_TupFromTlist = false;
    hjstate->hj_NeedNewOuter = true;
    hjstate->hj_NeedNewInner = true; 
    hjstate->hj_MatchedOuter = false;

    /* On configure les indicateurs pour l'état initial du Hash Join */
    hjstate->hj_innerExhausted = false; 
    hjstate->hj_outerExhausted = false; 
    hjstate->hj_foundByProbingInner = 0; 
    hjstate->hj_foundByProbingOuter = 0; 
    hjstate->hj_fetchingFromInner = true; 

    return hjstate;
}
/* Fonction pour compter le nombre de slots nécessaires pour le Hash Join */
int
ExecCountSlotsHashJoin(HashJoin *node)
{
    /* On additionne les slots requis par les sous-plans (relations intérieure et extérieure)
       et les slots spécifiques au Hash Join */
	return ExecCountSlotsNode(outerPlan(node)) +
		ExecCountSlotsNode(innerPlan(node)) +
		HASHJOIN_NSLOTS; // Définition du nombre fixe de slots pour le Hash Join
}

/* Fonction pour libérer les ressources allouées au Hash Join */
void
ExecEndHashJoin(HashJoinState *node)
{
    /* Si une table de hachage pour la relation intérieure a été créée, on la détruit */
	if (node->hj_InnerHashTable)
	{
		ExecHashTableDestroy(node->hj_InnerHashTable);
		node->hj_InnerHashTable = NULL; // On s'assure que le pointeur est réinitialisé
	}
    
    /* Si une table de hachage pour la relation extérieure a été créée, on la détruit également */
    if (node->hj_OuterHashTable) 
    {
        ExecHashTableDestroy(node->hj_OuterHashTable); 
        node->hj_OuterHashTable = NULL; // Réinitialisation du pointeur
    }

    /* On libère le contexte d'expression associé à ce nœud */
	ExecFreeExprContext(&node->js.ps);

    /* On nettoie tous les slots utilisés dans cet état */
	ExecClearTuple(node->js.ps.ps_ResultTupleSlot);
	ExecClearTuple(node->hj_OuterTupleSlot);
	ExecClearTuple(node->hj_InnerTupleSlot); 
	ExecClearTuple(node->hj_InnerHashTupleSlot);
	ExecClearTuple(node->hj_OuterHashTupleSlot); 

    /* On termine les sous-plans (nœuds enfants) pour libérer leurs ressources */
	ExecEndNode(outerPlanState(node));
	ExecEndNode(innerPlanState(node));
}

