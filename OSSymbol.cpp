/*
 * Copyright (c) 2000-2007 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 * 
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
/* IOSymbol.cpp created by gvdl on Fri 1998-11-17 */

#include "OSSymbol.h"

#define super OSString

typedef struct { int i, j; } OSSymbolPoolState;

#if OSALLOCDEBUG
extern "C" {
    extern int debug_container_malloc_size;
};
#define ACCUMSIZE(s) do { debug_container_malloc_size += (s); } while(0)
#else
#define ACCUMSIZE(s)
#endif

#define INITIAL_POOL_SIZE  (exp2ml(1 + log2(kInitBucketCount)))

#define GROW_FACTOR   (1)
#define SHRINK_FACTOR (3)

#define GROW_POOL()     do \
    if (count * GROW_FACTOR > nBuckets) { \
        reconstructSymbols(true); \
    } \
while (0)

#define SHRINK_POOL()     do \
    if (count * SHRINK_FACTOR < nBuckets && \
        nBuckets > INITIAL_POOL_SIZE) { \
        reconstructSymbols(false); \
    } \
while (0)

class OSSymbolPool
{
private:
    static const unsigned int kInitBucketCount = 16;

    typedef struct { unsigned int count; OSSymbol **symbolP; } Bucket;

    Bucket *buckets;
    unsigned int nBuckets;
    unsigned int count;
    int poolGate;

    static inline void hashSymbol(const char *s,
                                  unsigned int *hashP,
                                  unsigned int *lenP)
    {
        unsigned int hash = 0;
        unsigned int len = 0;

        /* Unroll the loop. */
        for (;;) {
            if (!*s) break; len++; hash ^= *s++;
            if (!*s) break; len++; hash ^= *s++ <<  8;
            if (!*s) break; len++; hash ^= *s++ << 16;
            if (!*s) break; len++; hash ^= *s++ << 24;
        }
        *lenP = len;
        *hashP = hash;
    }

    static unsigned long log2(unsigned int x);
    static unsigned long exp2ml(unsigned int x);

    void reconstructSymbols(void);
    void reconstructSymbols(bool grow);

public:
    static void *operator new(size_t size);
    static void operator delete(void *mem, size_t size);

    OSSymbolPool() { };
    OSSymbolPool(const OSSymbolPool *old);
    virtual ~OSSymbolPool();

    bool init();

    inline void closeGate() { };
    inline void openGate()  { };

    OSSymbol *findSymbol(const char *cString) const;
    OSSymbol *insertSymbol(OSSymbol *sym);
    void removeSymbol(OSSymbol *sym);

    OSSymbolPoolState initHashState();
    OSSymbol *nextHashState(OSSymbolPoolState *stateP);
};

void * OSSymbolPool::operator new(size_t size)
{
    void *mem = (void *)kalloc(size);
    ACCUMSIZE(size);
    assert(mem);
    bzero(mem, size);

    return mem;
}

void OSSymbolPool::operator delete(void *mem, size_t size)
{
    kfree(mem, size);
    ACCUMSIZE(-size);
}

bool OSSymbolPool::init()
{
    count = 0;
    nBuckets = INITIAL_POOL_SIZE;
    buckets = (Bucket *) kalloc(nBuckets * sizeof(Bucket));
    ACCUMSIZE(nBuckets * sizeof(Bucket));
	
	assert(buckets);
	
    if (!buckets)
	{
        return false;
	}

    bzero(buckets, nBuckets * sizeof(Bucket));

   // poolGate = lck_mtx_alloc_init(IOLockGroup, LCK_ATTR_NULL);
	poolGate = 1; /* hax */
	
    return poolGate != 0;
}

OSSymbolPool::OSSymbolPool(const OSSymbolPool *old)
{
    count = old->count;
    nBuckets = old->nBuckets;
    buckets = old->buckets;

    poolGate = 0;	// Do not duplicate the poolGate
}

OSSymbolPool::~OSSymbolPool()
{
    if (buckets) {
        kfree(buckets, nBuckets * sizeof(Bucket));
        ACCUMSIZE(-(nBuckets * sizeof(Bucket)));
    }

  //  if (poolGate)
    //    lck_mtx_free(poolGate, IOLockGroup);
}

unsigned long OSSymbolPool::log2(unsigned int x)
{
    unsigned long i;

    for (i = 0; x > 1 ; i++)
        x >>= 1;
    return i;
}

unsigned long OSSymbolPool::exp2ml(unsigned int x)
{
    return (1 << x) - 1;
}

OSSymbolPoolState OSSymbolPool::initHashState()
{
    OSSymbolPoolState newState = { nBuckets, 0 };
    return newState;
}

OSSymbol *OSSymbolPool::nextHashState(OSSymbolPoolState *stateP)
{
    Bucket *thisBucket = &buckets[stateP->i];

    while (!stateP->j) {
        if (!stateP->i)
            return 0;
        stateP->i--;
        thisBucket--;
        stateP->j = thisBucket->count;
    }

    stateP->j--;
    if (thisBucket->count == 1)
        return (OSSymbol *) thisBucket->symbolP;
    else
        return thisBucket->symbolP[stateP->j];
}

void OSSymbolPool::reconstructSymbols(void)
{
    this->reconstructSymbols(true);
}

void OSSymbolPool::reconstructSymbols(bool grow)
{
    unsigned int new_nBuckets = nBuckets;
    OSSymbol *insert;
    OSSymbolPoolState state;

    if (grow) {
        new_nBuckets += new_nBuckets + 1;
    } else {
       /* Don't shrink the pool below the default initial size.
        */
        if (nBuckets <= INITIAL_POOL_SIZE) {
            return;
        }
        new_nBuckets = (new_nBuckets - 1) / 2;
    }

   /* Create old pool to iterate after doing above check, cause it
    * gets finalized at return.
    */
    OSSymbolPool old(this);

    count = 0;
    nBuckets = new_nBuckets;
    buckets = (Bucket *) kalloc(nBuckets * sizeof(Bucket));
    ACCUMSIZE(nBuckets * sizeof(Bucket));
    /* @@@ gvdl: Zero test and panic if can't set up pool */
    bzero(buckets, nBuckets * sizeof(Bucket));

    state = old.initHashState();
    while ( (insert = old.nextHashState(&state)) )
        insertSymbol(insert);
}

OSSymbol *OSSymbolPool::findSymbol(const char *cString) const
{
    Bucket *thisBucket;
    unsigned int j, inLen, hash;
    OSSymbol *probeSymbol, **list;

    hashSymbol(cString, &hash, &inLen); inLen++;
    thisBucket = &buckets[hash % nBuckets];
    j = thisBucket->count;

    if (!j)
        return 0;

    if (j == 1) {
        probeSymbol = (OSSymbol *) thisBucket->symbolP;

        if (inLen == probeSymbol->length
        &&  (strncmp(probeSymbol->string, cString, probeSymbol->length) == 0))
            return probeSymbol;
	return 0;
    }

    for (list = thisBucket->symbolP; j--; list++) {
        probeSymbol = *list;
        if (inLen == probeSymbol->length
        &&  (strncmp(probeSymbol->string, cString, probeSymbol->length) == 0))
            return probeSymbol;
    }

    return 0;
}

OSSymbol *OSSymbolPool::insertSymbol(OSSymbol *sym)
{
    const char *cString = sym->string;
    Bucket *thisBucket;
    unsigned int j, inLen, hash;
    OSSymbol *probeSymbol, **list;

    hashSymbol(cString, &hash, &inLen); inLen++;
    thisBucket = &buckets[hash % nBuckets];
    j = thisBucket->count;

    if (!j) {
        thisBucket->symbolP = (OSSymbol **) sym;
        thisBucket->count++;
        count++;
        return sym;
    }

    if (j == 1) {
        probeSymbol = (OSSymbol *) thisBucket->symbolP;

        if (inLen == probeSymbol->length
        &&  strncmp(probeSymbol->string, cString, probeSymbol->length) == 0)
            return probeSymbol;

        list = (OSSymbol **) kalloc(2 * sizeof(OSSymbol *));
        ACCUMSIZE(2 * sizeof(OSSymbol *));
        /* @@@ gvdl: Zero test and panic if can't set up pool */
        list[0] = sym;
        list[1] = probeSymbol;
        thisBucket->symbolP = list;
        thisBucket->count++;
        count++;
        GROW_POOL();

        return sym;
    }

    for (list = thisBucket->symbolP; j--; list++) {
        probeSymbol = *list;
        if (inLen == probeSymbol->length
        &&  strncmp(probeSymbol->string, cString, probeSymbol->length) == 0)
            return probeSymbol;
    }

    j = thisBucket->count++;
    count++;
    list = (OSSymbol **) kalloc(thisBucket->count * sizeof(OSSymbol *));
    ACCUMSIZE(thisBucket->count * sizeof(OSSymbol *));
    /* @@@ gvdl: Zero test and panic if can't set up pool */
    list[0] = sym;
    bcopy(thisBucket->symbolP, list + 1, j * sizeof(OSSymbol *));
    kfree(thisBucket->symbolP, j * sizeof(OSSymbol *));
    ACCUMSIZE(-(j * sizeof(OSSymbol *)));
    thisBucket->symbolP = list;
    GROW_POOL();

    return sym;
}

void OSSymbolPool::removeSymbol(OSSymbol *sym)
{
    Bucket *thisBucket;
    unsigned int j, inLen, hash;
    OSSymbol *probeSymbol, **list;

    hashSymbol(sym->string, &hash, &inLen); inLen++;
    thisBucket = &buckets[hash % nBuckets];
    j = thisBucket->count;
    list = thisBucket->symbolP;

    if (!j)
        return;

    if (j == 1) {
        probeSymbol = (OSSymbol *) list;

        if (probeSymbol == sym) {
            thisBucket->symbolP = 0;
            count--;
            thisBucket->count--;
            SHRINK_POOL();
            return;
        }
        return;
    }

    if (j == 2) {
        probeSymbol = list[0];
        if (probeSymbol == sym) {
            thisBucket->symbolP = (OSSymbol **) list[1];
            kfree(list, 2 * sizeof(OSSymbol *));
	    ACCUMSIZE(-(2 * sizeof(OSSymbol *)));
            count--;
            thisBucket->count--;
            SHRINK_POOL();
            return;
        }

        probeSymbol = list[1];
        if (probeSymbol == sym) {
            thisBucket->symbolP = (OSSymbol **) list[0];
            kfree(list, 2 * sizeof(OSSymbol *));
	    ACCUMSIZE(-(2 * sizeof(OSSymbol *)));
            count--;
            thisBucket->count--;
            SHRINK_POOL();
            return;
        }
        return;
    }

    for (; j--; list++) {
        probeSymbol = *list;
        if (probeSymbol == sym) {

            list = (OSSymbol **)
                kalloc((thisBucket->count-1) * sizeof(OSSymbol *));
	    ACCUMSIZE((thisBucket->count-1) * sizeof(OSSymbol *));
            if (thisBucket->count-1 != j)
                bcopy(thisBucket->symbolP, list,
                      (thisBucket->count-1-j) * sizeof(OSSymbol *));
            if (j)
                bcopy(thisBucket->symbolP + thisBucket->count-j,
                      list + thisBucket->count-1-j,
                      j * sizeof(OSSymbol *));
            kfree(thisBucket->symbolP, thisBucket->count * sizeof(OSSymbol *));
	    ACCUMSIZE(-(thisBucket->count * sizeof(OSSymbol *)));
            thisBucket->symbolP = list;
            count--;
            thisBucket->count--;
            return;
        }
    }
}

/*
 *********************************************************************
 * From here on we are actually implementing the OSSymbol class
 *********************************************************************
 */
OSDefineMetaClassAndStructorsWithInit(OSSymbol, OSString,
                                      OSSymbol::initialize())
OSMetaClassDefineReservedUnused(OSSymbol, 0)
OSMetaClassDefineReservedUnused(OSSymbol, 1)
OSMetaClassDefineReservedUnused(OSSymbol, 2)
OSMetaClassDefineReservedUnused(OSSymbol, 3)
OSMetaClassDefineReservedUnused(OSSymbol, 4)
OSMetaClassDefineReservedUnused(OSSymbol, 5)
OSMetaClassDefineReservedUnused(OSSymbol, 6)
OSMetaClassDefineReservedUnused(OSSymbol, 7)

static OSSymbolPool *pool;

void OSSymbol::initialize()
{
    pool = new OSSymbolPool;
    assert(pool);

    if (!pool->init()) {
        delete pool;
        assert(false);
    };
}

bool OSSymbol::initWithCStringNoCopy(const char *) { return false; }
bool OSSymbol::initWithCString(const char *) { return false; }
bool OSSymbol::initWithString(const OSString *) { return false; }

const OSSymbol *OSSymbol::withString(const OSString *aString)
{
    // This string may be a OSSymbol already, cheap check.
    if (OSDynamicCast(OSSymbol, aString)) {
	aString->retain();
	return (const OSSymbol *) aString;
    }
    else if (((const OSSymbol *) aString)->flags & kOSStringNoCopy)
        return OSSymbol::withCStringNoCopy(aString->getCStringNoCopy());
    else
        return OSSymbol::withCString(aString->getCStringNoCopy());
}

const OSSymbol *OSSymbol::withCString(const char *cString)
{
    pool->closeGate();

    OSSymbol *oldSymb = pool->findSymbol(cString);
    if (!oldSymb) {
        OSSymbol *newSymb = new OSSymbol;
        if (!newSymb) {
            pool->openGate();
            return newSymb;
        }

	if (newSymb->OSString::initWithCString(cString))
	    oldSymb = pool->insertSymbol(newSymb);
        
        if (newSymb == oldSymb) {
            pool->openGate();
            return newSymb;	// return the newly created & inserted symbol.
        }
        else
            // Somebody else inserted the new symbol so free our copy
	    newSymb->OSString::free();
    }
    
    oldSymb->retain();	// Retain the old symbol before releasing the lock.

    pool->openGate();
    return oldSymb;
}

const OSSymbol *OSSymbol::withCStringNoCopy(const char *cString)
{
    pool->closeGate();

    OSSymbol *oldSymb = pool->findSymbol(cString);
    if (!oldSymb) {
        OSSymbol *newSymb = new OSSymbol;
        if (!newSymb) {
            pool->openGate();
            return newSymb;
        }

	if (newSymb->OSString::initWithCStringNoCopy(cString))
	    oldSymb = pool->insertSymbol(newSymb);
        
        if (newSymb == oldSymb) {
            pool->openGate();
            return newSymb;	// return the newly created & inserted symbol.
        }
        else
            // Somebody else inserted the new symbol so free our copy
	    newSymb->OSString::free();
    }
    
    oldSymb->retain();	// Retain the old symbol before releasing the lock.

    pool->openGate();
    return oldSymb;
}

void OSSymbol::checkForPageUnload(void *startAddr, void *endAddr)
{
    OSSymbol *probeSymbol;
    OSSymbolPoolState state;

    pool->closeGate();
    state = pool->initHashState();
    while ( (probeSymbol = pool->nextHashState(&state)) ) {
        if (probeSymbol->string >= startAddr && probeSymbol->string < endAddr) {
            const char *oldString = probeSymbol->string;

            probeSymbol->string = (char *) kalloc(probeSymbol->length);
	    ACCUMSIZE(probeSymbol->length);
            bcopy(oldString, probeSymbol->string, probeSymbol->length);
            probeSymbol->flags &= ~kOSStringNoCopy;
        }
    }
    pool->openGate();
}

void OSSymbol::taggedRelease(const void *tag) const
{
    super::taggedRelease(tag);
}

void OSSymbol::taggedRelease(const void *tag, const int when) const
{
    pool->closeGate();
    super::taggedRelease(tag, when);
    pool->openGate();
}

void OSSymbol::free()
{
    pool->removeSymbol(this);
    super::free();
}

bool OSSymbol::isEqualTo(const char *aCString) const
{
    return super::isEqualTo(aCString);
}

bool OSSymbol::isEqualTo(const OSSymbol *aSymbol) const
{
    return aSymbol == this;
}

bool OSSymbol::isEqualTo(const OSMetaClassBase *obj) const
{
    OSSymbol *	sym;
    OSString *	str;

    if ((sym = OSDynamicCast(OSSymbol, obj)))
	return isEqualTo(sym);
    else if ((str = OSDynamicCast(OSString, obj)))
	return super::isEqualTo(str);
    else
	return false;
}
