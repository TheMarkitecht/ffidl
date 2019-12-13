/* much of the contents of this file are borrowed from Tcl source code.
before distribution this must be replaced with new original
Jim code for licensing reasons.  or else migrate to some 
other dynamic string library.  */

#define JIM_DSTRING_STATIC_SIZE 200
typedef struct Jim_DString {
    char *string;		/* Points to beginning of string: either
				 * staticSpace below or a malloced array. */
    int length;			/* Number of non-NULL characters in the
				 * string. */
    int spaceAvl;		/* Total number of bytes available for the
				 * string and its terminating NULL char. */
    char staticSpace[JIM_DSTRING_STATIC_SIZE];
				/* Space to use in common case where string is
				 * small. */
} Jim_DString;

#define Jim_DStringLength(dsPtr) ((dsPtr)->length)
#define Jim_DStringValue(dsPtr) ((dsPtr)->string)
#define Jim_DStringTrunc Jim_DStringSetLength

/*
 *----------------------------------------------------------------------
 *
 * Jim_DStringInit --
 *
 *	Initializes a dynamic string, discarding any previous contents of the
 *	string (Jim_DStringFree should have been called already if the dynamic
 *	string was previously in use).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The dynamic string is initialized to be empty.
 *
 *----------------------------------------------------------------------
 */

void
Jim_DStringInit(
    Jim_DString *dsPtr)		/* Pointer to structure for dynamic string. */
{
    dsPtr->string = dsPtr->staticSpace;
    dsPtr->length = 0;
    dsPtr->spaceAvl = JIM_DSTRING_STATIC_SIZE;
    dsPtr->staticSpace[0] = '\0';
}

/*
 *----------------------------------------------------------------------
 *
 * Jim_DStringAppend --
 *
 *	Append more bytes to the current value of a dynamic string.
 *
 * Results:
 *	The return value is a pointer to the dynamic string's new value.
 *
 * Side effects:
 *	Length bytes from "bytes" (or all of "bytes" if length is less than
 *	zero) are added to the current value of the string. Memory gets
 *	reallocated if needed to accomodate the string's new size.
 *
 *----------------------------------------------------------------------
 */

char *
Jim_DStringAppend(
    Jim_DString *dsPtr,		/* Structure describing dynamic string. */
    const char *bytes,		/* String to append. If length is -1 then this
				 * must be null-terminated. */
    int length)			/* Number of bytes from "bytes" to append. If
				 * < 0, then append all of bytes, up to null
				 * at end. */
{
    int newSize;

    if (length < 0) {
	length = strlen(bytes);
    }
    newSize = length + dsPtr->length;

    /*
     * Allocate a larger buffer for the string if the current one isn't large
     * enough. Allocate extra space in the new buffer so that there will be
     * room to grow before we have to allocate again.
     */

    if (newSize >= dsPtr->spaceAvl) {
	dsPtr->spaceAvl = newSize * 2;
	if (dsPtr->string == dsPtr->staticSpace) {
	    char *newString = ckalloc(dsPtr->spaceAvl);

	    memcpy(newString, dsPtr->string, dsPtr->length);
	    dsPtr->string = newString;
	} else {
	    int offset = -1;

	    /* See [16896d49fd] */
	    if (bytes >= dsPtr->string
		    && bytes <= dsPtr->string + dsPtr->length) {
		offset = bytes - dsPtr->string;
	    }

	    dsPtr->string = ckrealloc(dsPtr->string, dsPtr->spaceAvl);

	    if (offset >= 0) {
		bytes = dsPtr->string + offset;
	    }
	}
    }

    /*
     * Copy the new string into the buffer at the end of the old one.
     */

    memcpy(dsPtr->string + dsPtr->length, bytes, length);
    dsPtr->length += length;
    dsPtr->string[dsPtr->length] = '\0';
    return dsPtr->string;
}

/*
 *----------------------------------------------------------------------
 *
 * Jim_DStringFree --
 *
 *	Frees up any memory allocated for the dynamic string and reinitializes
 *	the string to an empty state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The previous contents of the dynamic string are lost, and the new
 *	value is an empty string.
 *
 *----------------------------------------------------------------------
 */

void
Jim_DStringFree(
    Jim_DString *dsPtr)		/* Structure describing dynamic string. */
{
    if (dsPtr->string != dsPtr->staticSpace) {
	ckfree(dsPtr->string);
    }
    dsPtr->string = dsPtr->staticSpace;
    dsPtr->length = 0;
    dsPtr->spaceAvl = JIM_DSTRING_STATIC_SIZE;
    dsPtr->staticSpace[0] = '\0';
}
