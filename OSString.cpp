/*
 * Copyright (c) 2000 Apple Computer, Inc. All rights reserved.
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
/* IOString.m created by rsulack on Wed 17-Sep-1997 */
/* IOString.cpp converted to C++ on Tue 1998-9-22 */

#include "OSArray.h"
#include "OSString.h"
#include "OSSerialize.h"

#define kfree(ptr, size) kfree(ptr)
#define super OSObject

OSDefineMetaClassAndStructors(OSString, OSObject)
OSMetaClassDefineReservedUnused(OSString,  0)
OSMetaClassDefineReservedUnused(OSString,  1)
OSMetaClassDefineReservedUnused(OSString,  2)
OSMetaClassDefineReservedUnused(OSString,  3)
OSMetaClassDefineReservedUnused(OSString,  4)
OSMetaClassDefineReservedUnused(OSString,  5)
OSMetaClassDefineReservedUnused(OSString,  6)
OSMetaClassDefineReservedUnused(OSString,  7)
OSMetaClassDefineReservedUnused(OSString,  8)
OSMetaClassDefineReservedUnused(OSString,  9)
OSMetaClassDefineReservedUnused(OSString, 10)
OSMetaClassDefineReservedUnused(OSString, 11)
OSMetaClassDefineReservedUnused(OSString, 12)
OSMetaClassDefineReservedUnused(OSString, 13)
OSMetaClassDefineReservedUnused(OSString, 14)
OSMetaClassDefineReservedUnused(OSString, 15)

#if OSALLOCDEBUG
extern "C" {
    extern int debug_container_malloc_size;
};
#define ACCUMSIZE(s) do { debug_container_malloc_size += (s); } while(0)
#else
#define ACCUMSIZE(s)
#endif

bool OSString::initWithString(const OSString *aString)
{
    return initWithCString(aString->string);
}

bool OSString::initWithCString(const char *cString)
{
    if (!cString || !super::init())
        return false;
    length = strlen(cString) + 1;
    string = (char *) kalloc(length);
    if (!string)
        return false;
    bcopy(cString, string, length);
	
    ACCUMSIZE(length);
    return true;
}

bool OSString::initWithCStringNoCopy(const char *cString)
{
    if (!cString || !super::init())
        return false;
	
    length = strlen(cString) + 1;
    flags |= kOSStringNoCopy;
    string = const_cast<char *>(cString);
	
    return true;
}

OSString *OSString::withString(const OSString *aString)
{
    OSString *me = new OSString;
	
    if (me && !me->initWithString(aString)) {
        me->release();
        return 0;
    }
	
    return me;
}

extern "C" void* _ZN8OSString15initWithCStringEPKc();
extern "C" void* _ZTV8OSString();

OSString *OSString::withCString(const char *cString)
{
    OSString *me = new OSString;
	
    if (me && !me->initWithCString(cString)) {
        me->release();
        return 0;
    }
	
    return me;
}

OSString *OSString::withCStringNoCopy(const char *cString)
{
    OSString *me = new OSString;
	
    if (me && !me->initWithCStringNoCopy(cString)) {
        me->release();
        return 0;
    }
	
    return me;
}

void OSString::free()
{
    if ( !(flags & kOSStringNoCopy) && string) {
        kfree(string, (size_t)length);
        ACCUMSIZE(-length);
    }
	
    super::free();
}

unsigned int OSString::getLength()  const { return length - 1; }

const char *OSString::getCStringNoCopy() const
{
    return string;
}

bool OSString::setChar(char aChar, unsigned int index)
{
    if ( !(flags & kOSStringNoCopy) && index < length - 1) {
        string[index] = aChar;
		
        return true;
    }
    else
        return false;
}

char OSString::getChar(unsigned int index) const
{
    if (index < length)
        return string[index];
    else
        return '\0';
}


bool OSString::isEqualTo(const OSString *aString) const
{
    if (length != aString->length)
        return false;
    else
        return isEqualTo((const char *) aString->string);
}

bool OSString::isEqualTo(const char *aCString) const
{
    return strncmp(string, aCString, length) == 0;
}

bool OSString::isEqualTo(const OSMetaClassBase *obj) const
{

        return false;
}

bool OSString::isEqualTo(const OSData *obj) const
{
	return false;
}

bool OSString::serialize(OSSerialize *s) const
{
	char *c = string;
	
    if (s->previouslySerialized(this)) return true;
	
    if (!s->addXMLStartTag(this, "string")) return false;
    while (*c) {
        if (*c == '<') {
            if (!s->addString("&lt;")) return false;
        } else if (*c == '>') {
            if (!s->addString("&gt;")) return false;
        } else if (*c == '&') {
            if (!s->addString("&amp;")) return false;
        } else {
            if (!s->addChar(*c)) return false;
        }
        c++;
    }
	
    return s->addXMLEndTag("string");
}
