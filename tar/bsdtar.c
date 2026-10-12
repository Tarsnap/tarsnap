--- a/tar/bsdtar.c
+++ b/tar/bsdtar.c
@@ -924,9 +924,20 @@ configure_bsdtar(struct bsdtar *bsdtar)
 			bsdtar->configfile = config_read_one_file(bsdtar,
 			    "tarsnap.conf", NULL);
 		else {
-			if (getenv("XDG_CONFIG_HOME") != NULL &&
-			    getenv("XDG_CONFIG_HOME")[0] != '\0')
-				bsdtar->configfile = config_read_one_file(bsdtar,
-				    "tarsnap/tarsnap.conf",
-				    getenv("XDG_CONFIG_HOME"));
+			const char *xdg_config_home =
+			    getenv("XDG_CONFIG_HOME");
+			/* Per the XDG Base Directory spec, paths in
+			 * XDG_CONFIG_HOME must be absolute; a relative
+			 * value is ignored so that configuration is not
+			 * accidentally read from the current directory.
+			 */
+			if (xdg_config_home != NULL &&
+			    xdg_config_home[0] != '\0' &&
+			    xdg_config_home[0] != '/')
+				log_warnx(
+				    "Ignoring relative XDG_CONFIG_HOME=%s; "
+				    "falling back to ~/.config/tarsnap/tarsnap.conf",
+				    xdg_config_home);
+			if (xdg_config_home != NULL &&
+			    xdg_config_home[0] != '\0' &&
+			    xdg_config_home[0] == '/')
+				bsdtar->configfile = config_read_one_file(
+				    bsdtar, "tarsnap/tarsnap.conf",
+				    xdg_config_home);
 			else
 				bsdtar->configfile = config_read_one_file(
 				    bsdtar, "tarsnap.conf", NULL);
 		}
 	}
