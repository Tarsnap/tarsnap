/* ... conteúdo original de tar/bsdtar.c até a função dooption ... */
dooption(struct bsdtar *bsdtar, const char *conf_opt,
		    (bsdtar->bwlimit_rate_down < 8000) ||
		    (bsdtar->bwlimit_rate_down > 1000000000.))
			bsdtar_errc(bsdtar, 1, 0,
			    "Invalid bandwidth rate limit (8000 to 1000000000"
			    " bytes/s): %s", conf_arg);
		bsdtar->option_maxbw_rate_down_set = 1;
	} else if (strcmp(conf_opt, "maxbw-rate-up") == 0) {
		if (bsdtar->option_maxbw_rate_up_set)
			/* ... */
		if (bsdtar->bwlimit_rate_up < 8000 ||
		    (bsdtar->bwlimit_rate_up > 1000000000.))
			bsdtar_errc(bsdtar, 1, 0,
			    "Invalid bandwidth rate limit (8000 to 1000000000"
			    " bytes/s): %s", conf_arg);
		bsdtar->option_maxbw_rate_up_set = 1;
	} else if (strcmp(conf_opt, "noatime") == 0) {
/* ... conteúdo original até optset ... */
optset:
	/* Option specified multiple times. */
	if (fromconffile == 0) {
		bsdtar_warnc(bsdtar, 0,
		    "Option specified multiple times: --%s", conf_opt);
		usage(bsdtar);
	}
	return;
/* ... restante do arquivo original ... */
