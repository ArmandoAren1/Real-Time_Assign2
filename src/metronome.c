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



#define MY_PULSE_CODE   _PULSE_CODE_MINAVAIL
#define PAUSE_PULSE_CODE (MY_PULSE_CODE+1)
#define QUIT_PULSE_CODE	(MY_PULSE_CODE+2)

char data[255];

int server_coid;

typedef union {
    struct _pulse pulse;
    char msg[255];
} my_message_t;

struct signature {

} typedef signature_t;

DataTableRow t[] = {
{2, 4, 4, "|1&2&"},
{3, 4, 6, "|1&2&3&"},
{4, 4, 8, "|1&2&3&4&"},
{5, 4, 10, "|1&2&3&4-5-"},
{3, 8, 6, "|1-2-3-"},
{6, 8, 6, "|1&a2&a"},
{9, 8, 9, "|1&a2&a3&a"},
{12, 8, 12, "|1&a2&a3&a4&a"}};

int io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb)
{

	int nb;

	if (data == NULL) return 0;

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

	return(_RESMGR_NPARTS(1));
}

int io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb)
{
	int nb = 0;

		// verify if the msg was sent
	    if( msg->i.nbytes == ctp->info.msglen - (ctp->offset + sizeof(*msg) ))
	    {
	        /* have all the data */
		char *buf;
		char *alert_msg;
		int i, pauseAmount;
		buf = (char *)(msg+1);

		// did the client send ALERT
		if(strstr(buf, "pause") != NULL){
			for(i = 0; i < 2; i++){
				alert_msg = strsep(&buf, " ");
			}
			pauseAmount = atoi(alert_msg);
			if(pauseAmount >= 1 && pauseAmount <= 9){
				//FIXME :: replace getprio() with SchedGet()
				MsgSendPulse(server_coid, SchedGet(0,0,NULL), _PULSE_CODE_MINAVAIL, pauseAmount);
			} else {
				printf("Integer is not between 1 and 9.\n");
			}
		} else if (strstr(buf, "info") != NULL){
			//metronome [<bpm> beats/min, time signature <ts-top>/<ts-bottom>
			//// ////////
			printf(data, "% d beats/min, time signature %d/%d", tst, tsd );
			// TODO write the info here
		} else if (strstr(buf, "quit") != NULL){
			MsgSendPulse(server_coid, SchedGet(0,0,NULL), QUIT_PULSE_CODE, 0);
		} else {
			strcpy(data, buf);
		}

		nb = msg->i.nbytes;
	    }
	    _IO_SET_WRITE_NBYTES (ctp, nb);

	    if (msg->i.nbytes > 0)
	        ocb->attr->flags |= IOFUNC_ATTR_MTIME | IOFUNC_ATTR_CTIME;

	    return (_RESMGR_NPARTS (0));

}

int io_open(resmgr_context_t *ctp, io_open_t *msg, RESMGR_HANDLE_T *handle, void *extra)
{
	 if ((server_coid = name_open("metronome", 0)) == -1) {
	        perror("name_open failed.");
	        return EXIT_FAILURE;
	    }
	    return (iofunc_open_default (ctp, msg, handle, extra));
}

void *metronimeThread(/*void* argc*/){ // client
   struct sigevent         event;
   struct itimerspec       itime;
   timer_t                 timer_id;
   int                     rcvid;
   my_message_t            msg;
   pthread_attr_t 		   attr;
   int 					   nThreads;
   name_attach_t 		   *attach;


//	if ((attach = name_attach()) == NULL) {
//		return EXIT_FAILURE;
//	}

   event.sigev_notify = SIGEV_PULSE;
   event.sigev_coid = ConnectAttach(ND_LOCAL_NODE, 0, attach->chid, _NTO_SIDE_CHANNEL, 0);
   //event.sigev_priority = getprio(0);
   event.sigev_code = MY_PULSE_CODE;
   timer_create(CLOCK_REALTIME, &event, &timer_id);


    //#metronome 120 2 4
   /*60 sec / 120 beats = 0.5 sec / beat
    * 0.5 sec/beat * 2 beat/measure = 1 second per measure
    * (1 sec) / (4 intervals) = 0.25 sec /interval
    * */




   itime.it_value.tv_sec = 1;
   /* 500 million nsecs = .5 secs */
   itime.it_value.tv_nsec = 500000000;
   itime.it_interval.tv_sec = 1;
   /* 500 million nsecs = .5 secs */
   itime.it_interval.tv_nsec = 500000000;
   timer_settime(timer_id, 0, &itime, NULL);

   /*
    * As of the timer_settime(), we will receive our pulse
    * in 1.5 seconds (the itime.it_value) and every 1.5
    * seconds thereafter (the itime.it_interval)
    */

   for (;;) {
       rcvid = MsgReceive(chid, &msg, sizeof(msg), NULL);
       if (rcvid == 0) { /* we got a pulse */
            if (msg.pulse.code == MY_PULSE_CODE) {
                printf("we got a pulse from our timer\n");
            } /* else other pulses ... */
       } /* else other messages ... */
   }
   return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) { // server
	dispatch_t* dpp;
	resmgr_io_funcs_t io_funcs;
	resmgr_connect_funcs_t connect_funcs;
	iofunc_attr_t ioattr;
	dispatch_context_t   *ctp;
	pthread_attr_t attr;
	name_attach_t *attach;
	int id;

	if (argc != 4){
        perror("Invalid! you must have 4 arguments (beats-per-minute) (time-signature-top) (time-signature-bottom)");
        return EXIT_FAILURE;
	}

//	bpm = atoi(argv[1]);
//	tst = atoi(argv[2]);
//	tsb = atoi(argv[3]);

	dpp = dispatch_create();
	iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, _RESMGR_IO_NFUNCS, &io_funcs);
	connect_funcs.open = io_open;
	io_funcs.read = io_read;
	io_funcs.write = io_write;

	iofunc_attr_init(&ioattr, S_IFCHR | 0666, NULL, NULL);

	id = resmgr_attach(dpp, NULL, "/dev/local/metronome", _FTYPE_ANY, NULL, &connect_funcs, &io_funcs, &ioattr);

	ctp = dispatch_context_alloc(dpp);
	while(1) {
		ctp = dispatch_block(ctp);
		dispatch_handler(ctp);
	}
	return EXIT_SUCCESS;
}
