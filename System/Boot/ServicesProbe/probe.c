#include <special/ipc.h>
#include <sys/eventloop.h>
#include <punos/syscall/syscall.h>
int main(int argc,char **argv,char **envp)
{
    (void)argc;(void)argv;(void)envp;
    EventLoopHandle loop=NULL;
    if (eventloop_create(&loop)!=CORE_OK) return 1;
    Deadline deadline;
    if (time_deadline_after(20000,&deadline)!=CORE_OK) return 1;
    String endpoint={ (char *)"/run/punos/appserv.sock",23 };
    for (;;) {
        bool expired;
        if (time_deadline_expired(deadline,&expired)!=CORE_OK || expired) break;
        IpcConnectionHandle connection=NULL;
        if (ipc_connection_begin_connect(&endpoint,&connection)!=CORE_OK) {
            Event events[4];usize count;
            (void)eventloop_wait(loop,events,4,50,&count);continue;
        }
        i32 fd;EventSourceId source=0;
        if (ipc_connection_get_fd(connection,&fd)!=CORE_OK ||
            eventloop_add_fd(loop,fd,EVENT_READABLE|EVENT_WRITABLE,&source)!=CORE_OK) return 1;
        bool connected=false,sent=false,done=false;
        while (!done) {
            if (time_deadline_expired(deadline,&expired)!=CORE_OK || expired) break;
            Event events[4];usize count;
            if (eventloop_wait(loop,events,4,100,&count)!=CORE_OK) break;
            if (count==0) continue;
            if (!connected) {
                IpcConnectionState state;
                if (ipc_connection_finish_connect(connection,&state)!=CORE_OK) break;
                if (state!=IPC_CONNECTION_STATE_OPEN) continue;
                connected=true;
            }
            if (!sent) {
                IpcMessage request={1,{NULL,0}};
                if (ipc_connection_send(connection,&request)!=CORE_OK) break;
                sent=true;
            }
            bool pending;
            if (ipc_connection_flush(connection,&pending)!=CORE_OK) break;
            (void)eventloop_update_fd(loop,source,EVENT_READABLE|(pending?EVENT_WRITABLE:0));
            IpcReceiveStatus status;IpcMessage reply={0};
            if (ipc_connection_receive(connection,&status,&reply)!=CORE_OK || status==IPC_RECEIVE_STATUS_PEER_CLOSED) break;
            if (status==IPC_RECEIVE_STATUS_MESSAGE) {
                done=reply.type==2 && reply.payload.size>=6 && reply.payload.data[0]==0 && reply.payload.data[1]==0 && reply.payload.data[2]==0 && reply.payload.data[3]==0;
                (void)ipc_message_destroy(&reply);break;
            }
        }
        (void)eventloop_remove_source(loop,source);(void)ipc_connection_close(&connection);
        if (done) {
            const char text[]="PUNos services: appserv ready\n";
            (void)write(STDERR_FILENO,text,sizeof(text)-1);(void)eventloop_close(&loop);return 0;
        }
    }
    (void)eventloop_close(&loop);return 1;
}
