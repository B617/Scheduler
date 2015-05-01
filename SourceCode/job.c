#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include "job.h"

//#define DEBUG
//#define SHOW_UPDATE
//#define SHOW_INSTR
//#define SHOW_SELECT
//#define SHOW_SWITCH
//#define SHOW_SIGCHLD

int jobid=0;
int siginfo=1;
int fifo;
int globalfd;
int goon=0;
int preemption=0;

struct waitqueue *head=NULL;
struct waitqueue *next=NULL,*current =NULL;

void setGoon(){
	goon=1;
}

/* 调度程序 */
void scheduler()
{
	struct jobinfo *newjob=NULL;
	struct jobcmd cmd;
	int  count = 0;
	bzero(&cmd,DATALEN);
	if((count=read(fifo,&cmd,DATALEN))<0)
		error_sys("read fifo failed");
//	printf("%d\n",count);
#ifdef DEBUG
	printf("Reading whether other process send command!\n");
	if(count){
		printf("cmd cmdtype\t%d\ncmd defpri\t%d\ncmd data\t%s\n",cmd.type,cmd.defpri,cmd.data);
	}
	else
		printf("no data read\n");
#endif

	/* 更新等待队列中的作业 */
	#ifdef DEBUG
		printf("Update jobs in wait queue!\n");
	#endif

	
	#ifdef SHOW_UPDATE
		printf("before update\n");
		do_stat(cmd);
	#endif
	
	updateall();

	#ifdef SHOW_UPDATE
		printf("after update\n");
		do_stat(cmd);
	#endif

	switch(cmd.type){
	case ENQ:
		#ifdef DEBUG
			printf("Execute enq!\n");
		#endif
		#ifdef SHOW_INSTR
			printf("before enq\n");
			do_stat(cmd);
		#endif
		do_enq(newjob,cmd);
		#ifdef SHOW_INSTR
			printf("after enq\n");
			do_stat(cmd);
		#endif
		if(preemption){
			next=jobselect();
			jobswitch();
			preemption=0;
			return;
		}
		break;
	case DEQ:
		#ifdef DEBUG
			printf("Execute deq!\n");
		#endif
		#ifdef SHOW_INSTR
			printf("before deq\n");
			do_stat(cmd);
		#endif
		do_deq(cmd);
		#ifdef SHOW_INSTR
			printf("after deq\n");
			do_stat(cmd);
		#endif
		break;
	case STAT:
		#ifdef DEBUG
			printf("Execute stat\n");
		#endif
		#ifdef SHOW_INSTR
			printf("before stat\n");
			do_stat(cmd);
		#endif
		do_stat(cmd);
		#ifdef SHOW_INSTR
			printf("after stat\n");
			do_stat(cmd);
		#endif
		break;
	default:
		break;
	}
	
	#ifdef DEBUG
		printf("Select which job to run next!\n");
	#endif
	
//	if(head==NULL)
//		printf("before select,head=NULL\n");
	
//	printf("%d\n",canswitchjob());
	if(!canswitchjob()||!hasequalpri())
		return ;
	/* 选择高优先级作业 */
	next=jobselect();

//	showalljobs();

//	if(head==NULL)
//		printf("after select,head=NULL\n");

	#ifdef DEBUG
		printf("Switch to the next job!\n");
	#endif

	/* 作业切换 */
	#ifdef SHOW_SWITCH
		printf("before switch\n");
		do_stat(cmd);
	#endif
	jobswitch();
	#ifdef SHOW_SWITCH
		printf("after switch\n");
		do_stat(cmd);
	#endif

	showalljobs();
}

int allocjid()
{
	return ++jobid;
}

void updateall()
{
	struct waitqueue *p;

	/* 更新作业运行时间 */
	if(current){
		current->job->run_time += 1; /* 加1代表1000ms */
		current->job->round_time+=1;
//		printf("current_job_round_time=%d\n",current->job->round_time);
	}

	/* 更新作业等待时间及优先级 */
	for(p = head; p != NULL; p = p->next){
		p->job->wait_time += 1000;
		if(p->job->wait_time > 10000 && p->job->curpri < 3){
			p->job->curpri++;
			p->job->wait_time = 0;
		}
	}
}

int canswitchjob(){
	int t;
	if(current==NULL){
//		printf("current==NULL\n");
		return 1;
	}
	t=current->job->round_time;
//	printf("%d\n",t);
//	printf("%d\n",current->job->curpri);
	switch(current->job->curpri){
	case 1:
		return t>=5;
	case 2:
		return t>=2;
	case 3:
		return t>=1;
	default:
		return 1;
	}
}

int hasequalpri(){
	struct waitqueue *p;
	int nowpri;
//	showalljobs();
	if(current==NULL)
		return 1;
	nowpri=current->job->curpri;
	for(p=head;p!=NULL;p=p->next){
		if(p->job->curpri>=nowpri)
			return 1;
	}
	return 0;
}

struct waitqueue* jobselect()
{
	struct waitqueue *p,*prev,*select,*selectprev;
	int highest = -1;

	select = NULL;
	selectprev = NULL;
	if(head){
		/* 遍历等待队列中的作业，找到优先级最高的作业 */
		for(prev = head, p = head; p != NULL; prev = p,p = p->next)
			if(p->job->curpri > highest){
				select = p;
				selectprev = prev;
				highest = p->job->curpri;
			}
		selectprev->next = select->next;
		if (select == selectprev){
				head = head->next;
		}
	}
	#ifdef SHOW_SELECT
		printf("select job:\n");
		if(select==NULL){
			printf("NULL\n");
		}
		else{
			showjob(select->job);
		}
	#endif
	return select;
}

void jobswitch()
{
	struct waitqueue *p;
	int i;

	if(current && current->job->state == DONE){ /* 当前作业完成 */
		/* 作业完成，删除它 */
		for(i = 0;(current->job->cmdarg)[i] != NULL; i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i] = NULL;
		}
		/* 释放空间 */
		free(current->job->cmdarg);
		free(current->job);
		free(current);

		current = NULL;
	}

	if(next == NULL && current == NULL) /* 没有作业要运行 */

		return;
	else if (next != NULL && current == NULL){ /* 开始新的作业 */

		printf("begin start new job\n");
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		current->job->round_time=0;
		kill(current->job->pid,SIGCONT);
		return;
	}
	else if (next != NULL && current != NULL){ /* 切换作业 */

		printf("switch to Pid: %d\n",next->job->pid);
		kill(current->job->pid,SIGSTOP);
		current->job->curpri = current->job->defpri;
		current->job->wait_time = 0;
		current->job->round_time=0;
		current->job->state = READY;
		current->next=NULL;

		/* 放回等待队列 */
		if(head){
			for(p = head; p->next != NULL; p = p->next);
			p->next = current;
		}else{
			head = current;
		}
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		current->job->wait_time = 0;
		kill(current->job->pid,SIGCONT);
		return;
	}else{ /* next == NULL且current != NULL，不切换 */
		return;
	}
}

void sig_handler(int sig,siginfo_t *info,void *notused)
{
	int status;
	int ret;
	#ifdef SHOW_SIGCHLD
		struct jobcmd cmd;
	#endif

	switch (sig) {
case SIGVTALRM: /* 到达计时器所设置的计时间隔 */
	scheduler();
	#ifdef DEBUG
		printf("SIGVTALRM RECEIVED!\n");
	#endif
	return;
case SIGCHLD: /* 子进程结束时传送给父进程的信号 */
	#ifdef DEBUG
		printf("SIGCHLD RECEIVED!\n");
	#endif

	#ifdef SHOW_SIGCHLD
		do_stat(cmd);
	#endif
	ret = waitpid(-1,&status,WNOHANG);
	if (ret == 0)
		return;
	if(WIFEXITED(status)){//子进程正常退出
		current->job->state = DONE;
		printf("normal termation, exit status = %d\n",WEXITSTATUS(status));
	}else if (WIFSIGNALED(status)){//异常结束子进程
		printf("abnormal termation, signal number = %d\n",WTERMSIG(status));
	}else if (WIFSTOPPED(status)){//若为当前暂停子进程返回的状态，则为真；对于这种情况可执行WSTOPSIG(status)，取使子进程暂停的信号编号。
		printf("child stopped, signal number = %d\n",WSTOPSIG(status));
	}
	return;
	default:
		return;
	}
}

void do_enq(struct jobinfo *newjob,struct jobcmd enqcmd)
{
	struct waitqueue *newnode,*p;
	int i=0,pid;
	char *offset,*argvec,*q;
	char **arglist;
	sigset_t zeromask;

	sigemptyset(&zeromask);//初始化由set指定的信号集，信号集里面的所有信号被清空

	/* 封装jobinfo数据结构 */
	newjob = (struct jobinfo *)malloc(sizeof(struct jobinfo));
	newjob->jid = allocjid();
	newjob->defpri = enqcmd.defpri;
	newjob->curpri = enqcmd.defpri;
	newjob->ownerid = enqcmd.owner;
	newjob->state = READY;
	newjob->create_time = time(NULL);
	newjob->wait_time = 0;
	newjob->run_time = 0;
	newjob->round_time=0;
	arglist = (char**)malloc(sizeof(char*)*(enqcmd.argnum+1));
	newjob->cmdarg = arglist;
	offset = enqcmd.data;
	argvec = enqcmd.data;
	while (i < enqcmd.argnum){//将命令以':'为分隔符分隔命令，并存入newjob->cmdarg
		if(*offset == ':'){
			*offset++ = '\0';
			q = (char*)malloc(offset - argvec);
			strcpy(q,argvec);
			arglist[i++] = q;
			argvec = offset;
		}else
			offset++;
	}

	//判断是否可以抢占
	if(current!=NULL&&newjob->defpri>current->job->defpri){
		preemption=1;
	}

	arglist[i] = NULL;

#ifdef DEBUG

	printf("enqcmd argnum %d\n",enqcmd.argnum);
	for(i = 0;i < enqcmd.argnum; i++)
		printf("parse enqcmd:%s\n",arglist[i]);

#endif

	/*向等待队列中增加新的作业*/
	newnode = (struct waitqueue*)malloc(sizeof(struct waitqueue));
	newnode->next =NULL;
	newnode->job=newjob;

	if(head)
	{
		for(p=head;p->next != NULL; p=p->next);
		p->next =newnode;
	}else
		head=newnode;

	/*为作业创建进程*/
	if((pid=fork())<0)
		error_sys("enq fork failed");

	if(pid==0){
		newjob->pid =getpid();

		kill(getppid(),SIGUSR1);
		/*阻塞子进程,等等执行*/
		raise(SIGSTOP);
#ifdef DEBUG

		printf("begin running\n");
		for(i=0;arglist[i]!=NULL;i++)
			printf("arglist %s\n",arglist[i]);
#endif

		/*复制文件描述符到标准输出*/
		dup2(globalfd,1);
		/* 执行命令 */
		if(execv(arglist[0],arglist)<0)
			printf("exec failed\n");
		exit(1);
	}else{
		signal(SIGUSR1,setGoon);
		while(goon==0){
//			printf("father is waiting\n");
		}
		goon=0;
		newjob->pid=pid;
	}
}

void do_deq(struct jobcmd deqcmd)
{
	int deqid,i;
	struct waitqueue *p,*prev,*select,*selectprev;
	deqid=atoi(deqcmd.data);

#ifdef DEBUG
	printf("deq jid %d\n",deqid);
#endif

	/*current jodid==deqid,终止当前作业*/
	if (current && current->job->jid ==deqid){
		printf("teminate current job\n");
		kill(current->job->pid,SIGKILL);
		for(i=0;(current->job->cmdarg)[i]!=NULL;i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i]=NULL;
		}
		free(current->job->cmdarg);
		free(current->job);
		free(current);
		current=NULL;
	}
	else{ /* 或者在等待队列中查找deqid */
		select=NULL;
		selectprev=NULL;
		if(head){
			for(prev=head,p=head;p!=NULL;prev=p,p=p->next)
				if(p->job->jid==deqid){
					select=p;
					selectprev=prev;
					break;
				}
				selectprev->next=select->next;
				if(select==selectprev)
					head=NULL;
		}
		if(select){
			for(i=0;(select->job->cmdarg)[i]!=NULL;i++){
				free((select->job->cmdarg)[i]);
				(select->job->cmdarg)[i]=NULL;
			}
			free(select->job->cmdarg);
			free(select->job);
			free(select);
			select=NULL;
		}
	}
}

void do_stat(struct jobcmd statcmd)
{
	struct waitqueue *p;
	char timebuf[BUFLEN];
	/*
	*打印所有作业的统计信息:
	*1.作业ID
	*2.进程ID
	*3.作业所有者
	*4.作业运行时间
	*5.作业等待时间
	*6.作业创建时间
	*7.作业状态
	*/

	/* 打印信息头部 */
	printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
	if(current){
//		printf("current\n");
		strcpy(timebuf,ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			current->job->jid,
			current->job->pid,
			current->job->ownerid,
			current->job->run_time,
			current->job->wait_time,
			timebuf,"RUNNING");
	}
//	if(head==NULL){
//		printf("head=NULL\n");
//	}
	for(p=head;p!=NULL;p=p->next){
//		printf("queue\n");
		strcpy(timebuf,ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			timebuf,
			"READY");
	}
}

void showjob(struct jobinfo *job){
	char timebuf[BUFLEN];
	char *state;
	if(job==NULL){
		printf("NULL\n");
		return;
	}
	strcpy(timebuf,ctime(&(job->create_time)));
	timebuf[strlen(timebuf)-1]='\0';
	switch (job->state){
		case 0:
			state="READY";
			break;
		case 1:
			state="RUNNING";
			break;
		case 2:
			state="DONE";
			break;
		default:
			break;
	}
	printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCURPRI\tRONDT\tSTATE\n");
	printf("%d\t%d\t%d\t%d\t%d\t\t%d\t%d\t%s\n",
			job->jid,
			job->pid,
			job->ownerid,
			job->run_time,
			job->wait_time,
			job->curpri,
			job->round_time,
			state);
}

void showalljobs(){
	struct waitqueue *p;
	/*
	*打印所有作业的统计信息:
	*1.作业ID
	*2.进程ID
	*3.作业运行时间
	*4.作业等待时间
	*5.作业轮转时间
	*6.作业当前优先级
	*7.作业状态
	*/

	/* 打印信息头部 */
	printf("JOBID\tPID\tOWNER\tRUN_T\tWAIT_T\tROUND_T\tCURPRI\tSTATE\n");
	if(current){
		printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n",
			current->job->jid,
			current->job->pid,
			current->job->ownerid,
			current->job->run_time,
			current->job->wait_time,
			current->job->round_time,
			current->job->curpri,
			"RUNNING");
	}
	for(p=head;p!=NULL;p=p->next){
		printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			p->job->round_time,
			p->job->curpri,
			"READY");
	}
}

int main()
{
	struct timeval interval;
	struct itimerval new,old;
	struct stat statbuf;
	struct sigaction newact,oldact1,oldact2;

	#ifdef DEBUG
		printf("DEBUG IS OPEN!\n");
	#endif

	if(stat("/tmp/server",&statbuf)==0){//通过文件名获取文件信息，并保存在statbuf结构体中
		/* 如果FIFO文件存在,删掉 */
		if(remove("/tmp/server")<0)
			error_sys("remove failed");
	}

	if(mkfifo("/tmp/server",0666)<0)//mkfifo ()会依参数pathname建立特殊的FIFO文件，该文件必须不存在，而参数mode为该文件的权限
		error_sys("mkfifo failed");
	/* 在非阻塞模式下打开FIFO */
	if((fifo=open("/tmp/server",O_RDONLY|O_NONBLOCK))<0)
		error_sys("open fifo failed");

	/* 建立信号处理函数 */
	newact.sa_sigaction=sig_handler;
	sigemptyset(&newact.sa_mask);
	newact.sa_flags=SA_SIGINFO;
	sigaction(SIGCHLD,&newact,&oldact1);
	sigaction(SIGVTALRM,&newact,&oldact2);//实际时间报警时钟信号

	/* 设置时间间隔为1000毫秒 */
	interval.tv_sec=1;
	interval.tv_usec=0;

	new.it_interval=interval;
	new.it_value=interval;
	setitimer(ITIMER_VIRTUAL,&new,&old);

	while(siginfo==1);

	close(fifo);
	close(globalfd);
	return 0;
}

/*
How to get the first current?
do_enq
->	get newcode and head=newcode
->	next=jobselect=head
->	current=next
*/

/*
select & switch:
select:choose a job from the queue(queue.remove(job));
switch:switch and put the running job into the queue(queue.add(job));
*/
