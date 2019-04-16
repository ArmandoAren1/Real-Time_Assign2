#include <stdio.h>
#include <stdlib.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <sys/netmgr.h>
#include <sys/neutrino.h>

#define MY_PULSE_CODE _PULSE_CODE_MINAVAIL
#define PAUSE_PULSE_CODE (MY_PULSE_CODE+1)
#define QUIT_PULSE_CODE	(MY_PULSE_CODE+2)
#define INFO_PULSE_CODE (MY_PULSE_CODE+3)

void *metronomeThread(void* argc);

char data[255];

int server_coid;

pthread_t pthread;

typedef union {
	struct _pulse pulse;
	char msg[255];
} my_message_t;

struct threadAttribute {
	int bpm;
	int tst;
	int tsb;
	name_attach_t* attach;
}typedef threadAttribute_t;

struct DataTableRow {
	int tst;
	int tsb;
	int intervals;
	char output[13];
}typedef DataTableRow;

DataTableRow t[] = { { 2, 4, 4, "|1&2&" }, { 3, 4, 6, "|1&2&3&" }, { 4, 4, 8,
		"|1&2&3&4&" }, { 5, 4, 10, "|1&2&3&4-5-" }, { 3, 8, 6, "|1-2-3-" }, { 6,
		8, 6, "|1&a2&a" }, { 9, 8, 9, "|1&a2&a3&a" }, { 12, 8, 12,
		"|1&a2&a3&a4&a" } };

threadAttribute_t threadAttr;
int RUNNING = 1;

int io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb) {

	int nb;

	if (data == NULL)
		return 0;

	nb = strlen(data);

	//test to see if we have already sent the whole message.
	if (ocb->offset == nb)
		return 0;

	//We will return which ever is smaller the size of our data or the size of the buffer
	nb = min(nb, msg->i.nbytes);

	//Set the number of bytes we will return
	_IO_SET_READ_NBYTES(ctp, nb);

	//Copy data into reply buffer.
	SETIOV(ctp->iov, data, nb);

	//update offset into our data used to determine start position for next read.
	ocb->offset += nb;

	//If we are going to send any bytes update the access time for this resource.
	if (nb > 0)
		ocb->attr->flags |= IOFUNC_ATTR_ATIME;

	return (_RESMGR_NPARTS(1));
}

int io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb) {
	int nb = 0;

	// verify if the msg was sent
	if (msg->i.nbytes == ctp->info.msglen - (ctp->offset + sizeof(*msg))) {
		/* have all the data */
		//signature_t sig;
		char *buf;
		char *alert_msg;
		int i, pauseAmount;
		buf = (char *) (msg + 1);

		strcpy(data, buf);

		// did the client send ALERT
		alert_msg = strsep(&buf, " ");
		if (strstr(alert_msg, "pause") != NULL) {
			pauseAmount = atoi(strsep(&buf, " "));
			if (pauseAmount >= 1 && pauseAmount <= 9) {
				//FIXME :: replace getprio() with SchedGet()
				MsgSendPulse(server_coid, SchedGet(0, 0, NULL), PAUSE_PULSE_CODE, pauseAmount);
			} else {
				printf("\nInteger is not between 1 and 9.\n");
			}
		} else if (strstr(alert_msg, "info") != NULL) {
			MsgSendPulse(server_coid, SchedGet(0, 0, NULL), INFO_PULSE_CODE, 0);

		} else if (strstr(alert_msg, "quit") != NULL) {
			MsgSendPulse(server_coid, SchedGet(0, 0, NULL), QUIT_PULSE_CODE, 0);

			pthread_join(pthread, NULL);
			exit(EXIT_SUCCESS);
		} else {
			sprintf(data, "Error Incorrect Input\n");
		}

		nb = msg->i.nbytes;
	}
	_IO_SET_WRITE_NBYTES(ctp, nb);

	if (msg->i.nbytes > 0)
		ocb->attr->flags |= IOFUNC_ATTR_MTIME | IOFUNC_ATTR_CTIME;

	return (_RESMGR_NPARTS(0));

}

int io_open(resmgr_context_t *ctp, io_open_t *msg, RESMGR_HANDLE_T *handle,
		void *extra) {
	if ((server_coid = name_open("metronome", 0)) == -1) {
		perror("name_open failed.");
		return EXIT_FAILURE;
	}
	return (iofunc_open_default(ctp, msg, handle, extra));
}

void *metronomeThread(void* argc) {
	struct sigevent event;
	struct itimerspec itime;
	timer_t timer_id;
	int chid;
	int rcvid;
	int coid;
	int selected;
	my_message_t msg;
	threadAttribute_t* threadAttr;

	threadAttr = argc;
	chid = threadAttr->attach->chid;

	event.sigev_notify = SIGEV_PULSE;
	coid = ConnectAttach(ND_LOCAL_NODE, 0, chid, _NTO_SIDE_CHANNEL, 0);
	event.sigev_coid = coid;

	event.sigev_code = MY_PULSE_CODE;
	timer_create(CLOCK_REALTIME, &event, &timer_id);

	for (int i = 0; i < 8; i++) {
		if (t[i].tsb == threadAttr->tsb && t[i].tst == threadAttr->tst) {
			selected = i;
			break;
		}
	}

	double a = 60 / (double) threadAttr->bpm; // Seconds / Beat
	double b = a * (double) threadAttr->tst; // Seconds per measure
	double c = b / t[selected].intervals; // Intervals seconds

	int _c = (int) c;
	printf("Beats/second(a): %f\n", a);
	printf("Seconds per measure(b): %f\n", b);
	printf("Interval Seconds(c): %f\n", c);

	printf("Integer of c: %d\n", _c);

	printf("Value: %f\n", c * 1000000000);
	if (_c > 0){ // Interval is more than a second.
		itime.it_value.tv_sec = _c;
		itime.it_interval.tv_sec = _c;
		itime.it_value.tv_nsec = (c - _c) * 1000000000;
		itime.it_interval.tv_nsec = (c - _c) * 1000000000;
	} else {
		itime.it_value.tv_nsec = c * 1000000000;
		itime.it_interval.tv_nsec = c * 1000000000;
	}

	timer_settime(timer_id, 0, &itime, NULL);

	printf("String: %s\n", t[selected].output);

	setvbuf(stdout, NULL, _IONBF, 0);

	for (;;) {
		fprintf(stderr, "|");
		for (int i = 0; i < t[selected].intervals; i++) {

			rcvid = MsgReceive(chid, &msg, sizeof(msg), NULL);

			if (rcvid == -1){
				MsgError(rcvid, ENOSYS);
				return EXIT_FAILURE;
			}

			if (rcvid == 0) { /* we got a pulse */
				if (msg.pulse.code == MY_PULSE_CODE) {
					printf("%c", t[selected].output[i + 1]);
				}
				if (msg.pulse.code == QUIT_PULSE_CODE) {
					printf("quitting \n");

					timer_delete(timer_id); // Delete timer
					ConnectDetach(coid); // Detach Connection
					name_detach(threadAttr->attach, 0); //Detach from named channel
					name_close(server_coid);

					return EXIT_SUCCESS;
				}
				if (msg.pulse.code == PAUSE_PULSE_CODE) {
					itime.it_value.tv_sec = msg.pulse.value.sival_int;
					timer_settime(timer_id, 0, &itime, NULL);
				}
				if (msg.pulse.code == INFO_PULSE_CODE){
					sprintf(data, "%d beats/min\ttime signature %d/%d\n", threadAttr->bpm, threadAttr->tst, threadAttr->tsb);
				}
			}
		}
		printf("\n");
	}

	return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
	dispatch_t* dpp;
	resmgr_io_funcs_t io_funcs;
	resmgr_connect_funcs_t connect_funcs;
	iofunc_attr_t ioattr;
	dispatch_context_t *ctp;
	pthread_attr_t attr;
	name_attach_t *attach;
	threadAttribute_t threadAttr;
	int id;

	if (argc != 4) {
		perror("Invalid! you must have 4 arguments (beats-per-minute) (time-signature-top) (time-signature-bottom)");
		return EXIT_FAILURE;
	}

	if ((attach = name_attach(NULL, "metronome", 0)) == NULL) {
		return EXIT_FAILURE;
	}

	threadAttr.bpm = atoi(argv[1]);
	threadAttr.tst = atoi(argv[2]);
	threadAttr.tsb = atoi(argv[3]);
	threadAttr.attach = attach;

	dpp = dispatch_create();
	iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, _RESMGR_IO_NFUNCS, &io_funcs);
	connect_funcs.open = io_open;
	io_funcs.read = io_read;
	io_funcs.write = io_write;

	iofunc_attr_init(&ioattr, S_IFCHR | 0666, NULL, NULL);

	id = resmgr_attach(dpp, NULL, "/dev/local/metronome", _FTYPE_ANY, NULL, &connect_funcs, &io_funcs, &ioattr);

	pthread_attr_init(&attr);
	pthread_create(&pthread, &attr, &metronomeThread, &threadAttr);
	pthread_attr_destroy(&attr);

	ctp = dispatch_context_alloc(dpp);
	while (1) {
		ctp = dispatch_block(ctp);
		dispatch_handler(ctp);
	}

	return EXIT_SUCCESS;
}
