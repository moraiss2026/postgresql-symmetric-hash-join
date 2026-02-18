/* Fait par Ablouh Mohamed Amine (300322988) et Mohammed Raiss El Fenni (300296996) */
/*
 * Ce fichier implémente les fonctions nécessaires pour l'exécution du nœud Hash dans PostgreSQL.
 * Il inclut les fonctions pour initialiser le nœud Hash (`ExecInitHash`), exécuter le hachage des tuples (`ExecHash`),
 * créer et gérer la table de hachage utilisée pour les opérations de jointure (`ExecHashTableCreate`),
 * et gérer les cas où la table de hachage doit augmenter dynamiquement le nombre de batchs en mémoire (`ExecHashIncreaseNumBatches`).
 * Les fonctions principales manipulent les tuples hachés, calculent les valeurs de hachage,
 * insèrent les tuples dans la table de hachage, et assurent une gestion efficace de la mémoire.
 * Ce code est essentiel pour optimiser les opérations de jointure basées sur le hachage
 * dans le moteur de base de données PostgreSQL, améliorant ainsi les performances des requêtes impliquant de grandes tables.
 */

#include "postgres.h"

#include "executor/execdebug.h"
#include "executor/hashjoin.h"
#include "executor/instrument.h"
#include "executor/nodeHash.h"
#include "executor/nodeHashjoin.h"
#include "miscadmin.h"
#include "parser/parse_expr.h"
#include "utils/memutils.h"
#include "utils/lsyscache.h"

static void ExecHashIncreaseNumBatches(HashJoinTable hashtable);

TupleTableSlot * ExecHash(HashState *node)
{
    PlanState  *outerNode;
    List       *hashkeys;
    HashJoinTable hashtable;
    TupleTableSlot *slot;
    ExprContext *econtext;
    uint32      hashvalue;

    if (node->ps.instrument)
        InstrStartNode(node->ps.instrument);

    outerNode = outerPlanState(node);
    hashtable = node->hashtable;

    hashkeys = node->hashkeys;
    econtext = node->ps.ps_ExprContext;

    slot = ExecProcNode(outerNode);
    if (!outerNode || !hashkeys || !hashtable || !slot || !econtext) {
        elog(ERROR, "ExecHash: One or more critical components are NULL");
        return NULL;
    }

    if (!TupIsNull(slot)) {
        hashtable->totalTuples += 1;
        econtext->ecxt_innertuple = slot;
        econtext->ecxt_outertuple = slot;
        hashvalue = ExecHashGetHashValue(hashtable, econtext, hashkeys);
        ExecHashTableInsert(hashtable, ExecFetchSlotTuple(slot), hashvalue);
    } else {
        if (node->ps.instrument) {
            InstrStopNodeMulti(node->ps.instrument, hashtable->totalTuples);
        }
        return NULL;
    }
    return slot;
}

Node * MultiExecHash(HashState *node)
{
    PlanState  *outerNode;
    List       *hashkeys;
    HashJoinTable hashtable;
    TupleTableSlot *slot;
    ExprContext *econtext;
    uint32      hashvalue;

    if (node->ps.instrument)
        InstrStartNode(node->ps.instrument);

    outerNode = outerPlanState(node);
    hashtable = node->hashtable;

    hashkeys = node->hashkeys;
    econtext = node->ps.ps_ExprContext;

    for (;;) {
        slot = ExecProcNode(outerNode);
        if (TupIsNull(slot))
            break;
        hashtable->totalTuples += 1;
        econtext->ecxt_innertuple = slot;
        hashvalue = ExecHashGetHashValue(hashtable, econtext, hashkeys);
        ExecHashTableInsert(hashtable, ExecFetchSlotTuple(slot), hashvalue);
    }

    if (node->ps.instrument)
        InstrStopNodeMulti(node->ps.instrument, hashtable->totalTuples);

    return NULL;
}

HashState * ExecInitHash(Hash *node, EState *estate)
{
    HashState  *hashstate;

    SO_printf("ExecInitHash: initializing hash node\n");

    hashstate = makeNode(HashState);
    hashstate->ps.plan = (Plan *) node;
    hashstate->ps.state = estate;
    hashstate->hashtable = NULL;
    hashstate->hashkeys = NIL;

    ExecAssignExprContext(estate, &hashstate->ps);

#define HASH_NSLOTS 1

    ExecInitResultTupleSlot(estate, &hashstate->ps);

    hashstate->ps.targetlist = (List *)
        ExecInitExpr((Expr *) node->plan.targetlist, (PlanState *) hashstate);
    hashstate->ps.qual = (List *)
        ExecInitExpr((Expr *) node->plan.qual, (PlanState *) hashstate);

    outerPlanState(hashstate) = ExecInitNode(outerPlan(node), estate);

    ExecAssignResultTypeFromTL(&hashstate->ps);
    hashstate->ps.ps_ProjInfo = NULL;

    return hashstate;
}

int ExecCountSlotsHash(Hash *node)
{
    return ExecCountSlotsNode(outerPlan(node)) +
        ExecCountSlotsNode(innerPlan(node)) +
        HASH_NSLOTS;
}

void ExecEndHash(HashState *node)
{
    PlanState  *outerPlan;

    ExecFreeExprContext(&node->ps);

    outerPlan = outerPlanState(node);
    ExecEndNode(outerPlan);
}

HashJoinTable ExecHashTableCreate(Hash *node, List *hashOperators)
{
    HashJoinTable hashtable;
    Plan       *outerNode;
    int         nbuckets;
    int         nbatch;
    int         nkeys;
    int         i;
    ListCell   *ho;
    MemoryContext oldcxt;

    outerNode = outerPlan(node);

    ExecChooseHashTableSize(outerNode->plan_rows, outerNode->plan_width,
                            &nbuckets, &nbatch);

#ifdef HJDEBUG
    printf("nbatch = %d, nbuckets = %d\n", nbatch, nbuckets);
#endif

    hashtable = (HashJoinTable) palloc(sizeof(HashJoinTableData));
    hashtable->nbuckets = nbuckets;
    hashtable->buckets = NULL;
    hashtable->nbatch = nbatch;
    hashtable->curbatch = 0;
    hashtable->nbatch_original = nbatch;
    hashtable->nbatch_outstart = nbatch;
    hashtable->growEnabled = false;
    hashtable->totalTuples = 0;
    hashtable->innerBatchFile = NULL;
    hashtable->outerBatchFile = NULL;
    hashtable->spaceUsed = 0;
    hashtable->spaceAllowed = work_mem * 1024L;

    nkeys = list_length(hashOperators);
    hashtable->hashfunctions = (FmgrInfo *) palloc(nkeys * sizeof(FmgrInfo));
    i = 0;
    foreach(ho, hashOperators) {
        Oid         hashfn;

        hashfn = get_op_hash_function(lfirst_oid(ho));
        if (!OidIsValid(hashfn))
            elog(ERROR, "could not find hash function for hash operator %u",
                 lfirst_oid(ho));
        fmgr_info(hashfn, &hashtable->hashfunctions[i]);
        i++;
    }

    hashtable->hashCxt = AllocSetContextCreate(CurrentMemoryContext,
                                               "HashTableContext",
                                               ALLOCSET_DEFAULT_MINSIZE,
                                               ALLOCSET_DEFAULT_INITSIZE,
                                               ALLOCSET_DEFAULT_MAXSIZE);

    hashtable->batchCxt = AllocSetContextCreate(hashtable->hashCxt,
                                                "HashBatchContext",
                                                ALLOCSET_DEFAULT_MINSIZE,
                                                ALLOCSET_DEFAULT_INITSIZE,
                                                ALLOCSET_DEFAULT_MAXSIZE);

    oldcxt = MemoryContextSwitchTo(hashtable->hashCxt);

    if (nbatch > 1) {
        hashtable->innerBatchFile = (BufFile **)
            palloc0(nbatch * sizeof(BufFile *));
        hashtable->outerBatchFile = (BufFile **)
            palloc0(nbatch * sizeof(BufFile *));
    }

    MemoryContextSwitchTo(hashtable->batchCxt);

    hashtable->buckets = (HashJoinTuple *)
        palloc0(nbuckets * sizeof(HashJoinTuple));

    MemoryContextSwitchTo(oldcxt);

    return hashtable;
}

void ExecChooseHashTableSize(double ntuples, int tupwidth,
                              int *numbuckets,
                              int *numbatches)
{
    int         tupsize;
    double      inner_rel_bytes;
    long        hash_table_bytes;
    int         nbatch;
    int         nbuckets;
    int         i;

    if (ntuples <= 0.0)
        ntuples = 1000.0;

    tupsize = MAXALIGN(sizeof(HashJoinTupleData)) +
        MAXALIGN(sizeof(HeapTupleHeaderData)) +
        MAXALIGN(tupwidth);
    inner_rel_bytes = ntuples * tupsize;

    hash_table_bytes = work_mem * 1024L;

    if (inner_rel_bytes > hash_table_bytes) {
        long        lbuckets;
        double      dbatch;
        int         minbatch;

        lbuckets = (hash_table_bytes / tupsize) / NTUP_PER_BUCKET;
        lbuckets = Min(lbuckets, INT_MAX);
        nbuckets = (int) lbuckets;

        dbatch = ceil(inner_rel_bytes / hash_table_bytes);
        dbatch = Min(dbatch, INT_MAX / 2);
        minbatch = (int) dbatch;
        nbatch = 2;
        while (nbatch < minbatch)
            nbatch <<= 1;
    } else {
        double      dbuckets;

        dbuckets = ceil(ntuples / NTUP_PER_BUCKET);
        dbuckets = Min(dbuckets, INT_MAX);
        nbuckets = (int) dbuckets;

        nbatch = 1;
    }

    for (i = 0; i < (int) lengthof(hprimes); i++) {
        if (hprimes[i] >= nbuckets) {
            nbuckets = hprimes[i];
            break;
        }
    }

    *numbuckets = nbuckets;
    *numbatches = nbatch;
}

void ExecHashTableDestroy(HashJoinTable hashtable)
{
    int         i;

    for (i = 1; i < hashtable->nbatch; i++) {
        if (hashtable->innerBatchFile[i])
            BufFileClose(hashtable->innerBatchFile[i]);
        if (hashtable->outerBatchFile[i])
            BufFileClose(hashtable->outerBatchFile[i]);
    }

    MemoryContextDelete(hashtable->hashCxt);
    pfree(hashtable);
}

static void ExecHashIncreaseNumBatches(HashJoinTable hashtable)
{
    int         oldnbatch = hashtable->nbatch;
    int         curbatch = hashtable->curbatch;
    int         nbatch;
    int         i;
    MemoryContext oldcxt;
    long        ninmemory;
    long        nfreed;

    if (!hashtable->growEnabled)
        return;

    if (oldnbatch > INT_MAX / 2)
        return;

    nbatch = oldnbatch * 2;
    Assert(nbatch > 1);

    oldcxt = MemoryContextSwitchTo(hashtable->hashCxt);

    if (hashtable->innerBatchFile == NULL) {
        hashtable->innerBatchFile = (BufFile **)
            palloc0(nbatch * sizeof(BufFile *));
        hashtable->outerBatchFile = (BufFile **)
            palloc0(nbatch * sizeof(BufFile *));
    } else {
        hashtable->innerBatchFile = (BufFile **)
            repalloc(hashtable->innerBatchFile, nbatch * sizeof(BufFile *));
        hashtable->outerBatchFile = (BufFile **)
            repalloc(hashtable->outerBatchFile, nbatch * sizeof(BufFile *));
        MemSet(hashtable->innerBatchFile + oldnbatch, 0,
               (nbatch - oldnbatch) * sizeof(BufFile *));
        MemSet(hashtable->outerBatchFile + oldnbatch, 0,
               (nbatch - oldnbatch) * sizeof(BufFile *));
    }

    MemoryContextSwitchTo(oldcxt);

    hashtable->nbatch = nbatch;

    ninmemory = nfreed = 0;

    for (i = 0; i < hashtable->nbuckets; i++) {
        HashJoinTuple prevtuple;
        HashJoinTuple tuple;

        prevtuple = NULL;
        tuple = hashtable->buckets[i];

        while (tuple != NULL) {
            HashJoinTuple nexttuple = tuple->next;
            int         bucketno;
            int         batchno;

            ninmemory++;
            ExecHashGetBucketAndBatch(hashtable, tuple->hashvalue,
                                      &bucketno, &batchno);
            Assert(bucketno == i);
            if (batchno == curbatch) {
                prevtuple = tuple;
            } else {
                Assert(batchno > curbatch);
                ExecHashJoinSaveTuple(&tuple->htup, tuple->hashvalue,
                                      &hashtable->innerBatchFile[batchno]);
                if (prevtuple)
                    prevtuple->next = nexttuple;
                else
                    hashtable->buckets[i] = nexttuple;
                hashtable->spaceUsed -=
                    MAXALIGN(sizeof(HashJoinTupleData)) + tuple->htup.t_len;
                pfree(tuple);
                nfreed++;
            }

            tuple = nexttuple;
        }
    }

    if (nfreed == 0 || nfreed == ninmemory)
    {
        hashtable->growEnabled = false;
    }
}

void ExecHashTableInsert(HashJoinTable hashtable, HeapTuple tuple, uint32 hashvalue)
{
    int         bucketno;
    int         batchno;

    ExecHashGetBucketAndBatch(hashtable, hashvalue, &bucketno, &batchno);

    if (batchno == hashtable->curbatch) {
        HashJoinTuple hashTuple;
        int         hashTupleSize;

        hashTupleSize = MAXALIGN(sizeof(HashJoinTupleData)) + tuple->t_len;
        hashTuple = (HashJoinTuple) MemoryContextAlloc(hashtable->batchCxt, hashTupleSize);
        hashTuple->hashvalue = hashvalue;
        memcpy((char *) &hashTuple->htup,
               (char *) tuple,
               sizeof(hashTuple->htup));
        hashTuple->htup.t_datamcxt = hashtable->batchCxt;
        hashTuple->htup.t_data = (HeapTupleHeader)
            (((char *) hashTuple) + MAXALIGN(sizeof(HashJoinTupleData)));
        memcpy((char *) hashTuple->htup.t_data,
               (char *) tuple->t_data,
               tuple->t_len);
        hashTuple->next = hashtable->buckets[bucketno];
        hashtable->buckets[bucketno] = hashTuple;
        hashtable->spaceUsed += hashTupleSize;
        if (hashtable->spaceUsed > hashtable->spaceAllowed)
            ExecHashIncreaseNumBatches(hashtable);
    } else {
        Assert(batchno > hashtable->curbatch);
        ExecHashJoinSaveTuple(tuple, hashvalue, &hashtable->innerBatchFile[batchno]);
    }
}

uint32 ExecHashGetHashValue(HashJoinTable hashtable, ExprContext *econtext, List *hashkeys)
{
    uint32      hashkey = 0;
    ListCell   *hk;
    int         i = 0;
    MemoryContext oldContext;

    ResetExprContext(econtext);

    oldContext = MemoryContextSwitchTo(econtext->ecxt_per_tuple_memory);

    foreach(hk, hashkeys) {
        ExprState  *keyexpr = (ExprState *) lfirst(hk);
        Datum      keyval;
        bool       isNull;

        hashkey = (hashkey << 1) | ((hashkey & 0x80000000) ? 1 : 0);

        keyval = ExecEvalExpr(keyexpr, econtext, &isNull, NULL);

        if (!isNull) {
            uint32      hkey;

            hkey = DatumGetUInt32(FunctionCall1(&hashtable->hashfunctions[i], keyval));
            hashkey ^= hkey;
        }

        i++;
    }

    MemoryContextSwitchTo(oldContext);

    return hashkey;
}

void ExecHashGetBucketAndBatch(HashJoinTable hashtable, uint32 hashvalue, int *bucketno, int *batchno)
{
    uint32      nbuckets = (uint32) hashtable->nbuckets;
    uint32      nbatch = (uint32) hashtable->nbatch;

    if (nbatch > 1) {
        *bucketno = hashvalue % nbuckets;
        *batchno = (hashvalue / nbuckets) & (nbatch - 1);
    } else {
        *bucketno = hashvalue % nbuckets;
        *batchno = 0;
    }
}

HeapTuple ExecScanHashBucket(HashJoinState *hjstate, ExprContext *econtext)
{
    if (hjstate->hj_fetchingFromInner) {
        List *hjclauses = hjstate->hashclauses;
        HashJoinTable hashtable = hjstate->hj_OuterHashTable;
        HashJoinTuple hashTuple = hjstate->hj_OuterCurTuple;
        uint32 hashvalue = hjstate->hj_InnerCurHashValue;

        if (hashTuple == NULL)
            hashTuple = hashtable->buckets[hjstate->hj_OuterCurBucketNo];
        else
            hashTuple = hashTuple->next;
        while (hashTuple != NULL) {
            if (hashTuple->hashvalue == hashvalue) {
                HeapTuple heapTuple = &hashTuple->htup;
                TupleTableSlot *outtuple;
                outtuple = ExecStoreTuple(heapTuple, hjstate->hj_OuterHashTupleSlot, InvalidBuffer, false);
                econtext->ecxt_outertuple = outtuple;
                ResetExprContext(econtext);
                if (ExecQual(hjclauses, econtext, false)) {
                    hjstate->hj_OuterCurTuple = hashTuple;
                    return heapTuple;
                }
            }
            hashTuple = hashTuple->next;
        }
        return NULL;
    } else {
        List *hjclauses = hjstate->hashclauses;
        HashJoinTable hashtable = hjstate->hj_InnerHashTable;
        HashJoinTuple hashTuple = hjstate->hj_InnerCurTuple;
        uint32 hashvalue = hjstate->hj_OuterCurHashValue;

        if (hashTuple == NULL)
            hashTuple = hashtable->buckets[hjstate->hj_InnerCurBucketNo];
        else
            hashTuple = hashTuple->next;
        while (hashTuple != NULL) {
            if (hashTuple->hashvalue == hashvalue) {
                HeapTuple heapTuple = &hashTuple->htup;
                TupleTableSlot *inntuple;
                inntuple = ExecStoreTuple(heapTuple, hjstate->hj_InnerHashTupleSlot, InvalidBuffer, false);
                econtext->ecxt_innertuple = inntuple;
                ResetExprContext(econtext);
                if (ExecQual(hjclauses, econtext, false)) {
                    hjstate->hj_InnerCurTuple = hashTuple;
                    return heapTuple;
                }
            }
            hashTuple = hashTuple->next;
        }
        return NULL;
    }
}

void ExecHashTableReset(HashJoinTable hashtable)
{
    MemoryContext oldcxt;
    int nbuckets = hashtable->nbuckets;

    MemoryContextReset(hashtable->batchCxt);
    oldcxt = MemoryContextSwitchTo(hashtable->batchCxt);

    hashtable->buckets = (HashJoinTuple *)
        palloc0(nbuckets * sizeof(HashJoinTuple));

    hashtable->spaceUsed = 0;

    MemoryContextSwitchTo(oldcxt);
}

void ExecReScanHash(HashState *node, ExprContext *exprCtxt)
{
    if (((PlanState *) node)->lefttree->chgParam == NULL)
        ExecReScan(((PlanState *) node)->lefttree, exprCtxt);
}
