/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD: src/lib/libarchive/archive_read_support_format_tar.c,v 1.72 2008/12/06 06:45:15 kientzle Exp $");

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <stddef.h>
/* #include <stdint.h> */ /* See archive_platform.h */
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
+#include <limits.h>   /* for INT64_MAX */
 
 /* Obtain suitable wide-character manipulation functions. */
 #ifdef HAVE_WCHAR_H
 #include <wchar.h>
@@
 static void	 gnu_add_sparse_entry(struct tar *,
 	    off_t offset, off_t remaining);
@@
 static void	 gnu_add_sparse_entry(struct tar *tar,
 	    off_t offset, off_t remaining)
 {
-    struct sparse_block *sb;
-
-    sb = (struct sparse_block *)malloc(sizeof(*sb));
-    if (sb == NULL) {
-        /* Allocation failure will be caught later when we try to use the list. */
-        return;
-    }
-    sb->offset = offset;
-    sb->remaining = remaining;
-    sb->next = NULL;
-    if (tar->sparse_last == NULL) {
-        tar->sparse_list = sb;
-        tar->sparse_last = sb;
-    } else {
-        tar->sparse_last->next = sb;
-        tar->sparse_last = sb;
-    }
+    struct sparse_block *sb;
+
+    /*
+     * Guard against overflow of the logical sparse region.  The sum of
+     * offset and remaining must fit in an off_t (signed 64‑bit).  If it does
+     * not, the archive is malformed and we abort with a clear error.
+     */
+    if (offset < 0 || remaining < 0 ||
+        offset > INT64_MAX - remaining) {
+        archive_set_error(&tar->archive, ARCHIVE_ERRNO_MISC,
+            "Malformed GNU sparse map: offset+size overflow");
+        /* Do not add the entry; the caller will notice the error when it
+         * attempts to read data and will abort the extraction. */
+        return;
+    }
+
+    sb = (struct sparse_block *)malloc(sizeof(*sb));
+    if (sb == NULL) {
+        archive_set_error(&tar->archive, ENOMEM,
+            "Cannot allocate memory for sparse block");
+        return;
+    }
+    sb->offset = offset;
+    sb->remaining = remaining;
+    sb->next = NULL;
+    if (tar->sparse_last == NULL) {
+        tar->sparse_list = sb;
+        tar->sparse_last = sb;
+    } else {
+        tar->sparse_last->next = sb;
+        tar->sparse_last = sb;
+    }
 }
@@
 static int
 archive_read_format_tar_read_advance(struct archive_read *a,
 	    off_t offset)
 {
@@
-    if (tar->sparse_list != NULL) {
-        tar->sparse_list->offset += bytes_read;
-    }
+    if (tar->sparse_list != NULL) {
+        /* Prevent signed overflow when advancing the sparse offset. */
+        if (bytes_read < 0 ||
+            tar->sparse_list->offset > INT64_MAX - bytes_read) {
+            archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
+                "Signed overflow while advancing GNU sparse offset");
+            return (ARCHIVE_FATAL);
+        }
+        tar->sparse_list->offset += bytes_read;
+    }
*** End of file ***
 }
