#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <linux/wireless.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <mosquitto.h>

bool debug = false;
bool timeraction = false;

struct opt_info {
	char *lwt_topic;
	char *lwt_online;
	char *lwt_offline;
	char *wifi_topic;
} options;	/* Option values for callback routines */

void minute_timer(int signum)
{
	timeraction = true;
}

/* Get the Wifi signal strength for wlan0 and publish it as JSON string to the wifi_topic 
   Returns 0 on success, -1 on failure */
int publishSignalInfo(struct mosquitto *mosq, char *wifi_topic){
	int retval=0;
    struct iwreq req;
    struct iw_statistics *stats;

    //have to use a socket for ioctl
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (!mosq || !wifi_topic)
		return(-1);

    //make room for the iw_statistics object
    req.u.data.pointer = (struct iw_statistics *)malloc(sizeof(struct iw_statistics));
    req.u.data.length = sizeof(struct iw_statistics);

    //this will gather the signal strength
    strcpy(req.ifr_name, "wlan0");
    if(ioctl(sockfd, SIOCGIWSTATS, &req) == -1){
        //die with error, invalid interfacecd 
        fprintf(stderr, "Invalid interface.\n");
        return(-1);
    }
    else if(((struct iw_statistics *)req.u.data.pointer)->qual.updated & IW_QUAL_DBM){
 		char sigstrength [256];
        //signal is measured in dBm and is valid for us to use
 		snprintf(sigstrength, sizeof(sigstrength), "{Wifi: {RSSI: %d }}", 
		 			((int)(((struct iw_statistics *)req.u.data.pointer)->qual.level/2.56+0.5)));

		if (debug){
			fprintf(stderr, "sigstrength: %s\n", sigstrength);
		}

		if (mosquitto_publish(mosq, NULL, wifi_topic, strlen(sigstrength), sigstrength, 0, false)){
			fprintf(stderr, "Unable to publish WiFI strength message");
			retval = -1;
		}
    }

    close(sockfd);

	return(retval);
}

void my_log_callback(struct mosquitto *mosq, void *userdata, int level, const char *str)
{
	/* Pring all log messages regardless of level. */
	if (debug) fprintf(stderr, "%s\n", str);
}

/* After receiving CONACK, set LWT (if option set) and WiFi strength (if option set) */
void my_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
	struct opt_info *optptr = (struct opt_info *) userdata;

	if(!result){
		/* Setup LWT if LWT option set. */
		if (optptr->lwt_topic)
			if (mosquitto_publish(mosq, NULL, optptr->lwt_topic, strlen(optptr->lwt_online), optptr->lwt_online, 0, true))
				fprintf(stderr, "Unable to publish LWT message");
			
		/* Send initial WiFi strength if WiFi topic set */
		if (optptr->wifi_topic) {
			if (publishSignalInfo(mosq, optptr->wifi_topic))
				fprintf(stderr, "Can't publish initial WiFi strength");
			
			alarm(60); //Set next Wifi report for 60s
		}
	} else
		fprintf(stderr, "Connect failed\n");
}

/* This tool will check a Yi camera for motion detection.
    If the -c capture option is specified it will watch for capture files, once per second. If found, it will then wait
	for a further minute before checking again every second.

	If -c is not specified it will watch the log.txt file for "got a new motion start", once per second.

	Once motion is detected it will send an MQTT message to your MQTT broker to indicate that the alarm has triggered. 

	It also mimics the MQTT functionality of the popular Tasmota firmware for the equally popular Sonoff WiFi relay
	in (optionally) using LWT for disconnect notification and (optionally) reporting WiFi signal strength on a 
	minute-by-minute basis.

	Options are:

	-d (optonal - debugging messages)
	-h <host>
	-t <alarm topic>
	-m <alarm message>
	-c <path to copy the JPG and MP4 captures to> (optional - if not specified, just watches log.txt for motion start)
	-l or --lwt-topic <LWT topic> (optional)
	-n or --lwt-online <LWT online message> (mandatory if --lwt-topic specified)
	-f or --lwt-offline <LWT offline message> (mandatory if --lwt-topic specified)
	-w or --wifi-topic <WiFi strength topic> (optional)
	-p <port> (optional, defaults to 1883)
	-k <seconds> (optional, defaults to 60s)
*/

int main(int argc, char *argv[])
{
	char *host = NULL;
	int port = 1883;
	int keepalive = 60;
	char *alarm_topic, *alarm_message;
	char *lwt_topic=NULL, *lwt_online=NULL, *lwt_offline=NULL;
	char *wifi_topic=NULL; 
	char *capture_path=NULL;

	while (true) {
        int this_option_optind = optind ? optind : 1;
        int c, option_index = 0;
        static struct option long_options[] = {
            {"lwt-topic",   required_argument, 0,  'l' },
            {"lwt-online",  required_argument, 0,  'n' },
            {"lwt-offline", required_argument, 0,  'f' },
            {"wifi-topic", required_argument, 0,  'w' },
            {0,         0,                 0,  0 }
        };

        c = getopt_long(argc, argv, "dh:t:m:l:n:f:w:p:k:c:",
                 long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 0:
          if (long_options[option_index].flag != 0)
            break;
          printf ("option %s", long_options[option_index].name);
          if (optarg)
            printf (" with arg %s", optarg);
          printf ("\n");
          break;

		case 'd':
			debug = true;
			break;

        case 'h':
	   		host = optarg;
            break;

        case 't':
            alarm_topic = optarg;
            break;

        case 'm':
            alarm_message = optarg;
            break;

        case 'l':
            lwt_topic = optarg;
            break;

        case 'n':
            lwt_online = optarg;
            break;

		case 'f':
            lwt_offline = optarg;
            break;

        case 'w':
            wifi_topic = optarg;
            break;

        case 'p':
            port = atoi(optarg);
            break;

        case 'k':
            keepalive = atoi(optarg);
            break;

		case 'c':
			capture_path = optarg;
			break;

        case '?':
            break;

        default:
            abort();
        }
    }

	if (debug) {
		fprintf(stderr, "host: %s, port %d, keepalive %d\n", host, port, keepalive);
		fprintf(stderr, "alarm topic: %s, message: %s\n", alarm_topic, alarm_message);
		fprintf(stderr, "LWT topic: %s, online: %s, offline: %s\n", lwt_topic, lwt_online, lwt_offline);
		fprintf(stderr, "WiFi topic: %s\n", wifi_topic);
	}

	if (!host || !alarm_topic || !alarm_message || 
			((lwt_topic || lwt_online || lwt_offline) && !(lwt_topic && lwt_online && lwt_offline))
			) {
		fprintf(stderr, "Usage: %s [-d] -h host -t topic -m message [-p port] [-k keepalive]\n", argv[0]);
		fprintf(stderr, "          -c capture-path\n");
		fprintf(stderr, "          [--lwt-topic topic --lwt-online online --lwt-offline offline]\n");
		fprintf(stderr, "          [--wifi-topic wifistrength");
		abort();
	}

	/* That's all the option handling out of the way ... main functionality in the following code block */
	{
		FILE *fp;
		struct mosquitto *mosq = NULL;

		options.lwt_topic = lwt_topic;
		options.lwt_online = lwt_online;
		options.lwt_offline = lwt_offline;
		options.wifi_topic = wifi_topic;

		mosquitto_lib_init();
		mosq = mosquitto_new(NULL, true, &options);

		if(!mosq){
			fprintf(stderr, "Error: Out of memory.\n");
			return 1;
		}
		mosquitto_log_callback_set(mosq, my_log_callback);
		mosquitto_connect_callback_set(mosq, my_connect_callback);

		if (mosquitto_will_set(mosq, lwt_topic, strlen(lwt_offline), lwt_offline, 0, true)){
			fprintf(stderr, "Unable to set LWT");
		}

		if (wifi_topic)
			if (signal(SIGALRM, minute_timer) == SIG_ERR){
				fprintf(stderr, "Unable to set minute timer handler.\n");
				return 1;
			}

		if(mosquitto_connect(mosq, host, port, keepalive)){
			fprintf(stderr, "Unable to connect.\n");
			return 1;
		}

		if (!capture_path){
			int flags;

			if (fp=fopen("/tmp/log.txt", "r")){
				fprintf(stderr, "Unable to open /tmp/log.txt.\n");
				return 1;
			}
			fseek(fp, 0, SEEK_END);
			flags = fcntl(fileno(fp), F_GETFL, 0);
			fcntl(fileno(fp), F_SETFL, flags | O_NONBLOCK);
		}

		do {
			bool mocap = false;
			
			mosquitto_loop(mosq, -1, 1);

			if (timeraction) {
				if (publishSignalInfo(mosq, wifi_topic)){
					fprintf(stderr, "Unable to publish signal info\n");
					break;
				}

				timeraction = false;
				alarm(60);
			}

			if (capture_path){
				struct stat buf;

				if (!stat("/tmp/motion.mp4", &buf)){
					char cpycmd[256];

					if(debug) 
						fprintf(stderr, "Motion detected\n");

					snprintf(cpycmd, sizeof(cpycmd), "cp -p /tmp/motion.jpg %s/temp.jpg", capture_path);
					system(cpycmd);
					snprintf(cpycmd, sizeof(cpycmd), "cp -p /tmp/motion.mp4 %s/temp.mp4", capture_path);
					system(cpycmd);

					mocap = true;
				}
			} else {
				char *line = NULL;
				size_t len = 0;

				mocap = (getline(&line, &len, fp) > 0) && strstr(line, "got a new motion start");
				free(line);
			}

			if (mocap) {
				if (debug) fprintf(stderr, "Motion detected\n");

				if (mosquitto_publish(mosq, NULL, alarm_topic, strlen(alarm_message), alarm_message, 0, false)){
					fprintf(stderr, "Unable to publish alarm message");
					break;
				}
				
				if (capture_path) sleep(59);
			}
			sleep(1);
		} while (true);

		if (!capture_path)
			fclose(fp);

		mosquitto_destroy(mosq);
		mosquitto_lib_cleanup();
	}

	return 0;
}