AC_DEFUN([GP_CHECK_SNIPPETSMENU],
[
    LIBXML_VERSION=2.6.27

    GP_ARG_DISABLE([snippetsmenu], [auto])
    GP_CHECK_PLUGIN_DEPS([snippetsmenu], [LIBXML],
                         [libxml-2.0 >= ${LIBXML_VERSION}])
    GP_STATUS_PLUGIN_ADD([snippetsmenu], [$enable_snippetsmenu])

    AC_CONFIG_FILES([
        snippetsmenu/Makefile
        snippetsmenu/src/Makefile
        snippetsmenu/snippets/Makefile
    ])
])
