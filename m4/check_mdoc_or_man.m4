# CHECK_MDOC_OR_MAN
# -----------------
# If 'nroff -mdoc' or 'mandoc -mdoc' returns with an exit status of 0, we will
# install man pages which use mdoc macros; otherwise, we will install man pages
# which have had mdoc macros stripped out.  On systems which support them, the
# mdoc versions are preferable; but we err on the side of caution -- there may
# be systems where man(1) does not use nroff(1) but still have mdoc macros
# available, yet we will use the mdoc-less man pages on them.
AC_DEFUN([CHECK_MDOC_OR_MAN],
[if nroff -mdoc </dev/null >/dev/null 2>/dev/null; then
	MANVER=mdoc;
elif mandoc -mdoc </dev/null >/dev/null 2>/dev/null; then
	MANVER=mdoc;
else
	MANVER=man;
fi
AC_SUBST([MANVER])
])# CHECK_MDOC_OR_MAN
