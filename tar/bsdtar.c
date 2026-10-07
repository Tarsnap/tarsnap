```c
		case OPTION_STRIP_COMPONENTS: /* GNU tar 1.15 */
			{
				char *eptr;
				long l;

				errno = 0;
				l = strtol(bsdtar->optarg, &eptr, 10);
				if ((errno != 0) ||
				    (eptr == bsdtar->optarg) ||
				    (*eptr != '\0') || (l < 0) ||
				    (l > INT_MAX))
					bsdtar_errc(bsdtar, 1, 0,
					    "Invalid --strip-components"
					    " argument: %s", bsdtar->optarg);
				bsdtar->strip_components = (int)l;
			}
			break;
