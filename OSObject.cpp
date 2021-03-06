/*
 * Copyright (c) 2000 Apple Inc. All rights reserved.
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
/* OSObject.cpp created by gvdl on Fri 1998-11-17 */

#include "OSObject.h"
#include "OSSerialize.h"
#include "OSCollection.h"

extern "C" bool OSAtomicCompareAndSwap32( u_int32_t __oldValue, u_int32_t __newValue, volatile u_int32_t *__theValue );

#define OSCompareAndSwap OSAtomicCompareAndSwap32

#define ACCUMSIZE(s)

// OSDefineMetaClassAndAbstractStructors(OSObject, 0);
/* Class global data */
OSObject::MetaClass OSObject::gMetaClass;
const OSMetaClass * const OSObject::metaClass = &OSObject::gMetaClass;
const OSMetaClass * const OSObject::superClass = 0;

/* Class member functions - Can't use defaults */
OSObject::OSObject()			{ retainCount = 1; }
OSObject::OSObject(const OSMetaClass *)	{ retainCount = 1; }
OSObject::~OSObject()			{ }
const OSMetaClass * OSObject::getMetaClass() const
    { return &gMetaClass; }
OSObject *OSObject::MetaClass::alloc() const { return 0; }

/* The OSObject::MetaClass constructor */
OSObject::MetaClass::MetaClass()
    : OSMetaClass("OSObject", OSObject::superClass, sizeof(OSObject))
    { }

// Virtual Padding
OSMetaClassDefineReservedUnused(OSObject,  0)
OSMetaClassDefineReservedUnused(OSObject,  1)
OSMetaClassDefineReservedUnused(OSObject,  2)
OSMetaClassDefineReservedUnused(OSObject,  3)
OSMetaClassDefineReservedUnused(OSObject,  4)
OSMetaClassDefineReservedUnused(OSObject,  5)
OSMetaClassDefineReservedUnused(OSObject,  6)
OSMetaClassDefineReservedUnused(OSObject,  7)
OSMetaClassDefineReservedUnused(OSObject,  8)
OSMetaClassDefineReservedUnused(OSObject,  9)
OSMetaClassDefineReservedUnused(OSObject, 10)
OSMetaClassDefineReservedUnused(OSObject, 11)
OSMetaClassDefineReservedUnused(OSObject, 12)
OSMetaClassDefineReservedUnused(OSObject, 13)
OSMetaClassDefineReservedUnused(OSObject, 14)
OSMetaClassDefineReservedUnused(OSObject, 15)

static const char *getClassName(const OSObject *obj)
{
    const OSMetaClass *meta = obj->getMetaClass();
    return (meta) ? meta->getClassName() : "unknown class?";
}

bool OSObject::init()
    { return true; }

#if (!__ppc__) || (__GNUC__ < 3)

// Implemented in assembler in post gcc 3.x systems as we have a problem
// where the destructor in gcc2.95 gets 2 arguments.  The second argument
// appears to be a flag argument.  I have copied the assembler from Puma xnu
// to OSRuntimeSupport.c  So for 2.95 builds use the C 
void OSObject::free()
{
    const OSMetaClass *meta = getMetaClass();

    if (meta)
	meta->instanceDestructed();
    delete this;
}
#endif /* (!__ppc__) || (__GNUC__ < 3) */

int OSObject::getRetainCount() const
{
    return (int) ((u_int16_t) retainCount);
}

void OSObject::taggedRetain(const void *tag) const
{
    volatile u_int32_t *countP = (volatile u_int32_t *) &retainCount;
    u_int32_t inc = 1;
    u_int32_t origCount;
    u_int32_t newCount;

    // Increment the collection bucket.
    if ((const void *) OSTypeID(OSCollection) == tag)
	inc |= (1UL<<16);

    do {
	origCount = *countP;
        if ( ((u_int16_t) origCount | 0x1) == 0xffff ) {
            const char *msg;
            if (origCount & 0x1) {
                // If count == 0xffff that means we are freeing now so we can
                // just return obviously somebody is cleaning up dangling
                // references.
                msg = "Attempting to retain a freed object";
            }
            else {
                // If count == 0xfffe then we have wrapped our reference count.
                // We should stop counting now as this reference must be
                // leaked rather than accidently wrapping around the clock and
                // freeing a very active object later.

#if !DEBUG
		break;	// Break out of update loop which pegs the reference
#else /* DEBUG */
                // @@@ gvdl: eventually need to make this panic optional
                // based on a boot argument i.e. debug= boot flag
                msg = "About to wrap the reference count, reference leak?";
#endif /* !DEBUG */
            }
            panic("OSObject::refcount: %s", msg);
        }

	newCount = origCount + inc;
    } while (!OSCompareAndSwap(origCount, newCount, const_cast<u_int32_t *>(countP)));
}

void OSObject::taggedRelease(const void *tag) const
{
    taggedRelease(tag, 1);
}

void OSObject::taggedRelease(const void *tag, const int when) const
{
    volatile u_int32_t *countP = (volatile u_int32_t *) &retainCount;
    u_int32_t dec = 1;
    u_int32_t origCount;
    u_int32_t newCount;
    u_int32_t actualCount;

    // Increment the collection bucket.
    if ((const void *) OSTypeID(OSCollection) == tag)
	dec |= (1UL<<16);

    do {
	origCount = *countP;
        
        if ( ((u_int16_t) origCount | 0x1) == 0xffff ) {
            if (origCount & 0x1) {
                // If count == 0xffff that means we are freeing now so we can
                // just return obviously somebody is cleaning up some dangling
                // references.  So we blow out immediately.
                return;
            }
            else {
                // If count == 0xfffe then we have wrapped our reference
                // count.  We should stop counting now as this reference must be
                // leaked rather than accidently freeing an active object later.

#if !DEBUG
		return;	// return out of function which pegs the reference
#else /* DEBUG */
                // @@@ gvdl: eventually need to make this panic optional
                // based on a boot argument i.e. debug= boot flag
                panic("OSObject::refcount: %s",
                      "About to unreference a pegged object, reference leak?");
#endif /* !DEBUG */
            }
        }
	actualCount = origCount - dec;
        if ((u_int16_t) actualCount < when)
            newCount = 0xffff;
        else
            newCount = actualCount;

    } while (!OSCompareAndSwap(origCount, newCount, const_cast<u_int32_t *>(countP)));

    //
    // This panic means that we have just attempted to release an object
    // whose retain count has gone to less than the number of collections
    // it is a member off.  Take a panic immediately.
    // In fact the panic MAY not be a registry corruption but it is 
    // ALWAYS the wrong thing to do.  I call it a registry corruption 'cause
    // the registry is the biggest single use of a network of collections.
    //
// xxx - this error message is overly-specific;
// xxx - any code in the kernel could trip this,
// xxx - and it applies as noted to all collections, not just the registry
    if ((u_int16_t) actualCount < (actualCount >> 16)) {
        panic("A kext releasing a(n) %s has corrupted the registry.",
            getClassName(this));
    }

    // Check for a 'free' condition and that if we are first through
    if (newCount == 0xffff) {
        (const_cast<OSObject *>(this))->free();
    }
}

void OSObject::release() const
{
    taggedRelease(0);
}

void OSObject::retain() const
{
    taggedRetain(0);
}

void OSObject::release(int when) const
{
    taggedRelease(0, when);
}

bool OSObject::serialize(OSSerialize *s) const
{
    if (s->previouslySerialized(this)) return true;
	
    if (!s->addXMLStartTag(this, "string")) return false;
	
    if (!s->addString(getClassName(this))) return false;
    if (!s->addString(" is not serializable")) return false;
    
    return s->addXMLEndTag("string");
}

/* gah */
struct OSObjectTracking {};

void *OSObject::operator new(size_t size)
{
    OSObjectTracking * mem = (OSObjectTracking *) kalloc(size);
	
    bzero(mem, size);

    ACCUMSIZE(size);

    return (void *) mem;
}

void OSObject::operator delete(void *_mem, size_t size)
{
    kfree(_mem);

    ACCUMSIZE(-size);
}
