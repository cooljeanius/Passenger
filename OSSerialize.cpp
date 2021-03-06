/*
 * Copyright (c) 2000-2006 Apple Computer, Inc. All rights reserved.
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
/* OSSerialize.cpp created by rsulack on Wen 25-Nov-1998 */


#include "OSDictionary.h"
#include "OSSerialize.h"
#include "OSString.h"

#define super OSObject

OSDefineMetaClassAndStructors(OSSerialize, OSObject)
OSMetaClassDefineReservedUnused(OSSerialize, 0)
OSMetaClassDefineReservedUnused(OSSerialize, 1)
OSMetaClassDefineReservedUnused(OSSerialize, 2)
OSMetaClassDefineReservedUnused(OSSerialize, 3)
OSMetaClassDefineReservedUnused(OSSerialize, 4)
OSMetaClassDefineReservedUnused(OSSerialize, 5)
OSMetaClassDefineReservedUnused(OSSerialize, 6)
OSMetaClassDefineReservedUnused(OSSerialize, 7)

#if OSALLOCDEBUG
extern "C" {
    extern int debug_container_malloc_size;
};
#define ACCUMSIZE(s) do { debug_container_malloc_size += (s); } while(0)
#else
#define ACCUMSIZE(s)
#endif

char * OSSerialize::text() const
{
	return data;
}

void OSSerialize::clearText()
{
	bzero((void *)data, capacity);
	length = 1;
	tag = 0;
	tags->flushCollection();
}

bool OSSerialize::previouslySerialized(const OSMetaClassBase *o)
{
	char temp[16];
	OSString *tagString;

	// look it up
	tagString = (OSString *)tags->getObject((const OSSymbol *) o);

// xx-review: no error checking here for addString calls!
	// does it exist?
	if (tagString) {
		addString("<reference IDREF=\"");
		addString(tagString->getCStringNoCopy());
		addString("\"/>");
		return true;
	}

	// build a tag
	snprintf(temp, sizeof(temp), "%u", tag++);
	tagString = OSString::withCString(temp);

	// add to tag dictionary
        tags->setObject((const OSSymbol *) o, tagString);// XXX check return
	tagString->release();

	return false;
}

bool OSSerialize::addXMLStartTag(const OSMetaClassBase *o, const char *tagString)
{

	if (!addChar('<')) return false;
	if (!addString(tagString)) return false;
	if (!addString(" ID=\"")) return false;
	if (!addString(((OSString *)tags->getObject((const OSSymbol *)o))->getCStringNoCopy())) 
		return false;
	if (!addChar('\"')) return false;
	if (!addChar('>')) return false;
	return true;
}

bool OSSerialize::addXMLEndTag(const char *tagString)
{

	if (!addChar('<')) return false;
	if (!addChar('/')) return false;
	if (!addString(tagString)) return false;
	if (!addChar('>')) return false;
	return true;
}

bool OSSerialize::addChar(const char c)
{
	// add char, possibly extending our capacity
	if (length >= capacity && length >=ensureCapacity(capacity+capacityIncrement))
		return false;

	data[length - 1] = c;
	length++;
 
	return true;
}

bool OSSerialize::addString(const char *s)
{
	bool rc = false;

	while (*s && (rc = addChar(*s++))) ;

	return rc;
}

bool OSSerialize::initWithCapacity(unsigned int inCapacity)
{
    if (!super::init())
            return false;

    tags = OSDictionary::withCapacity(32);
    if (!tags) {
        return false;
    }

    tag = 0;
    length = 1;
    capacity = (inCapacity) ? inCapacity : (100);
    capacityIncrement = capacity;

    // allocate from the kernel map so that we can safely map this data
    // into user space (the primary use of the OSSerialize object)
    
    data = (char*)kalloc(capacity);
	assert(data);
	
    bzero((void *)data, capacity);


    ACCUMSIZE(capacity);

    return true;
}

OSSerialize *OSSerialize::withCapacity(unsigned int inCapacity)
{
	OSSerialize *me = new OSSerialize;

	if (me && !me->initWithCapacity(inCapacity)) {
		me->release();
		return 0;
	}

	return me;
}

unsigned int OSSerialize::getLength() const { return length; }
unsigned int OSSerialize::getCapacity() const { return capacity; }
unsigned int OSSerialize::getCapacityIncrement() const { return capacityIncrement; }
unsigned int OSSerialize::setCapacityIncrement(unsigned int increment)
{
    capacityIncrement = (increment)? increment : 256;
    return capacityIncrement;
}

unsigned int OSSerialize::ensureCapacity(unsigned int newCapacity)
{
	char *newData;
	int newSize;
	int oldSize;
	
	if (newCapacity <= capacity)
		return capacity;

    newCapacity = (((newCapacity - 1) / capacityIncrement) + 1)
	* capacityIncrement;
    newSize = sizeof(char) * newCapacity;
	
    newData = (char*) kalloc(newSize);
    if (newData) {
        oldSize = sizeof(char) * capacity;
		
        bcopy((void*)data, (void*)newData, oldSize);
        bzero(&newData[capacity], newSize - oldSize);
		
        kfree((void*)data, 0);
		
        data = newData;
        capacity = newCapacity;
    }
	else {
		assert(false);
	}

	return capacity;
}

void OSSerialize::free()
{
    if (tags)
        tags->release();

    if (data) {
		kfree(data, capacity);
        ACCUMSIZE( -capacity );
    }
    super::free();
}


OSDefineMetaClassAndStructors(OSSerializer, OSObject)

OSSerializer * OSSerializer::forTarget( void * target,
                               OSSerializerCallback callback, void * ref )
{
    OSSerializer * thing;

    thing = new OSSerializer;
    if( thing && !thing->init()) {
	thing->release();
	thing = 0;
    }

    if( thing) {
	thing->target	= target;
        thing->ref	= ref;
        thing->callback = callback;
    }
    return( thing );
}

bool OSSerializer::serialize( OSSerialize * s ) const
{
    return( (*callback)(target, ref, s) );
}
