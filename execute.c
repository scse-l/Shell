#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <glob.h>

#include "global.h"
#define DEBUG
//#define DEBUG_LJL
//#define DEBUG_LJL_GLOB
//#define DEBUG_LJL_PIPE

#define FIFO "/tmp/my_fifo"
int goon = 0, ingnore = 0;       //用于设置signal信号量
char *envPath[10], cmdBuff[40];  //外部命令的存放路径及读取外部命令的缓冲空间
History history;                 //历史命令
Job *head = NULL;                //作业头指针
pid_t fgPid;                     //当前前台作业的进程号

/*******************************************************
                  工具以及辅助方法
********************************************************/
/*判断命令是否存在*/
//F_OK这个变量表示文件存在(即00)
//access函数原型：
/*
int   access(const   char   *filename,   int   amode); 
amode参数为0时表示检查文件的存在性，如果文件存在，返回0，不存在，返回-1。 
这个函数还可以检查其它文件属性： 
06     检查读写权限 
04     检查读权限 
02     检查写权限 
01     检查执行权限 
00     检查文件的存在性
*/
int exists(char *cmdFile){
    int i = 0;
    if((cmdFile[0] == '/' || cmdFile[0] == '.') && access(cmdFile, F_OK) == 0){ //命令在当前目录
        strcpy(cmdBuff, cmdFile);
        return 1;
    }else{  //查找ysh.conf文件中指定的目录，确定命令是否存在
        while(envPath[i] != NULL){ //查找路径已在初始化时设置在envPath[i]中
            strcpy(cmdBuff, envPath[i]);
            strcat(cmdBuff, cmdFile);
            
            if(access(cmdBuff, F_OK) == 0){ //命令文件被找到
                return 1;
            }
            
            i++;
        }
    }
    
    return 0; 
}

/*将字符串转换为整型的Pid*/
int str2Pid(char *str, int start, int end){
    int i, j;
    char chs[20];
    
    for(i = start, j= 0; i < end; i++, j++){
        if(str[i] < '0' || str[i] > '9'){
            return -1;
        }else{
            chs[j] = str[i];
        }
    }
    chs[j] = '\0';
    
    return atoi(chs);
}

/*调整部分外部命令的格式*/
//为什么要这么做？
//只保留了最后‘/’之后的参数
void justArgs(char *str){
    int i, j, len;
    len = strlen(str);
    
    for(i = 0, j = -1; i < len; i++){
        if(str[i] == '/'){
            j = i;
        }
    }

    if(j != -1){ //找到符号'/'
        for(i = 0, j++; j < len; i++, j++){
            str[i] = str[j];
        }
        str[i] = '\0';
    }
	#ifdef DEBUG_LJL
		printf("Format of arguement has been changed\n");
	#endif
}

/*设置goon*/
void setGoon(){
    goon = 1;
	#ifdef DEBUG_LJL
	printf("Value of the signal goon has been changed\n");
	#endif
}

/*释放环境变量空间*/
void release(){
    int i;
    for(i = 0; strlen(envPath[i]) > 0; i++){
        free(envPath[i]);
    }
}

/*******************************************************
                  信号以及jobs相关
********************************************************/
/*添加新的作业*/
Job* addJob(pid_t pid){
    Job *now = NULL, *last = NULL, *job = (Job*)malloc(sizeof(Job));
    
	//初始化新的job
    job->pid = pid;
    strcpy(job->cmd, inputBuff);
    strcpy(job->state, RUNNING);
    job->next = NULL;
    
    if(head == NULL){ //若是第一个job，则设置为头指针
        head = job;
    }else{ //否则，根据pid将新的job插入到链表的合适位置
		now = head;
		while(now != NULL && now->pid < pid){
			last = now;
			now = now->next;
		}
        last->next = job;
        job->next = now;
    }
	#ifdef DEBUG_LJL
		printf("Jobs has been added\n");
	#endif
    return job;
}

/*移除一个作业*/
void rmJob(int sig, siginfo_t *sip, void* noused){
    pid_t pid;
    Job *now = NULL, *last = NULL;
    
    //这个信号量的是值在哪儿修改为1:fg命令、bg命令、ctrl+Z命令
    //这个信号量的作用？
    if(ingnore == 1){
        ingnore = 0;
        return;
    }
    
    pid = sip->si_pid;

    now = head;
	while(now != NULL && now->pid < pid){
		last = now;
		now = now->next;
	}
    
    if(now == NULL){ //作业不存在，则不进行处理直接返回
        return;
    }

	if(now->pid != fgPid)
	{
		wait(NULL);
	}    
	//开始移除该作业
    if(now == head){
        head = now->next;
    }else{
        last->next = now->next;
    }
    
    free(now);
}

//组合键命令ctrl+C
void ctrl_C()
{

	#ifdef DEBUG_LJL
		printf("In the function ctrl_C\n");
	#endif

	if(fgPid == 0)
	//没有前台作业，直接返回
	{
	#ifdef DEBUG_LJL
		printf("No front job\n");
	#endif
		return ;
	}

	//输出相关信息
	printf("The front job(Pis:%d) killed\n",fgPid);

	kill(fgPid,SIGQUIT);//结束前台进程
	fgPid = 0;//重置fgPid，表明没有前台作业

}

/*组合键命令ctrl+z*/
void ctrl_Z(){
    Job *now = NULL;
    
	#ifdef DEBUG_LJL
		printf("In the function ctrl_Z,fgPid:%d\n",fgPid);
	#endif
    if(fgPid == 0){ //前台没有作业则直接返回
	#ifdef DEBUG_LJL
		printf("No front job\n");
	#endif
        return;
    }
    
    //SIGCHLD信号产生自ctrl+z
    ingnore = 1;
    
	now = head;
	while(now != NULL && now->pid != fgPid)
		now = now->next;
    
    if(now == NULL){ //未找到前台作业，则根据fgPid添加前台作业
        now = addJob(fgPid);
    }
    
	//修改前台作业的状态及相应的命令格式，并打印提示信息
    strcpy(now->state, STOPPED); 
    now->cmd[strlen(now->cmd)] = '&';
    now->cmd[strlen(now->cmd) + 1] = '\0';
    printf("\n[%d]\t%s\t\t%s\n", now->pid, now->state, now->cmd);
    
	//发送SIGSTOP信号给正在前台运作的工作，将其停止
    kill(fgPid, SIGSTOP);
    fgPid = 0;
}

/*fg命令*/
void fg_exec(int pid){    
    Job *now = NULL; 
	int i;
    
    //SIGCHLD信号产生自此函数
    ingnore = 1;
    
	//根据pid查找作业
    now = head;
	while(now != NULL && now->pid != pid)
		now = now->next;
    
    if(now == NULL){ //未找到作业
        printf("pid为7%d 的作业不存在！\n", pid);
        return;
    }

    //记录前台作业的pid，修改对应作业状态
    fgPid = now->pid;
    strcpy(now->state, RUNNING);
    
    signal(SIGTSTP, ctrl_Z); //设置signal信号，为下一次按下组合键Ctrl+Z做准备
    signal(SIGINT, ctrl_C); //设置signal信号，为下一次按下组合键Ctrl+C做准备
    i = strlen(now->cmd) - 1;
    while(i >= 0 && now->cmd[i] != '&')
		i--;
    now->cmd[i] = '\0';
    
    printf("%s\n", now->cmd);
    kill(now->pid, SIGCONT); //向对象作业发送SIGCONT信号，使其运行
    waitpid(fgPid, NULL, 0); //父进程等待前台进程的运行
}

/*bg命令*/
void bg_exec(int pid){
    Job *now = NULL;
    
    //SIGCHLD信号产生自此函数
    ingnore = 1;
    
	//根据pid查找作业
	now = head;
    while(now != NULL && now->pid != pid)
		now = now->next;
    
    if(now == NULL){ //未找到作业
        printf("pid为7%d 的作业不存在！\n", pid);
        return;
    }
    
    strcpy(now->state, RUNNING); //修改对象作业的状态
    printf("[%d]\t%s\t\t%s\n", now->pid, now->state, now->cmd);
    
    kill(now->pid, SIGCONT); //向对象作业发送SIGCONT信号，使其运行
}

/*******************************************************
                    命令历史记录
********************************************************/
void addHistory(char *cmd){
    if(history.end == -1){ //第一次使用history命令
        history.end = 0;
        strcpy(history.cmds[history.end], cmd);
        return;
	}
    
    history.end = (history.end + 1)%HISTORY_LEN; //end前移一位
    strcpy(history.cmds[history.end], cmd); //将命令拷贝到end指向的数组中
    
    if(history.end == history.start){ //end和start指向同一位置
        history.start = (history.start + 1)%HISTORY_LEN; //start前移一位
    }
}

/*******************************************************
                     初始化环境
********************************************************/
/*通过路径文件获取环境路径*/
void getEnvPath(int len, char *buf){
    int i, j, last = 0, pathIndex = 0, temp;
    char path[40];
    
    for(i = 0, j = 0; i < len; i++){
        if(buf[i] == ':'){ //将以冒号(:)分隔的查找路径分别设置到envPath[]中
            if(path[j-1] != '/'){
                path[j++] = '/';
            }
            path[j] = '\0';
            j = 0;
            
            temp = strlen(path);
            envPath[pathIndex] = (char*)malloc(sizeof(char) * (temp + 1));
            strcpy(envPath[pathIndex], path);
            
            pathIndex++;
        }else{
            path[j++] = buf[i];
        }
    }
    
    envPath[pathIndex] = NULL;
}

/*初始化操作*/
void init(){
    int fd, n, len;
    char c, buf[80];

	//打开查找路径文件ysh.conf
    if((fd = open("ysh.conf", O_RDONLY, 660)) == -1){
        perror("init environment failed\n");
        exit(1);
    }
    
	//初始化history链表
    history.end = -1;
    history.start = 0;
    
    len = 0;
	//将路径文件内容依次读入到buf[]中
    while(read(fd, &c, 1) != 0){ 
        buf[len++] = c;
    }
    buf[len] = '\0';

    //将环境路径存入envPath[]
    getEnvPath(len, buf); 
    
    //注册信号
    struct sigaction action;
    action.sa_sigaction = rmJob;
    sigfillset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction(SIGCHLD, &action, NULL);
    signal(SIGTSTP, ctrl_Z);
	signal(SIGUSR1, setGoon);
	signal(SIGINT, ctrl_C);
}

/*******************************************************
                      命令解析
********************************************************/
SimpleCmd* handleSimpleCmdStr(int begin, int end){
    int i, j, k;
    int fileFinished; //记录命令是否解析完毕
	int pipeCmdFinished;  //记录管道命令是否解析完毕
    char c, buff[10][40], inputFile[30], outputFile[30], *temp = NULL;
    SimpleCmd *cmd = (SimpleCmd*)malloc(sizeof(SimpleCmd));
    SimpleCmd *testCmd;


	//默认为非后台命令，输入输出重定向为null
    cmd->isBack = 0;
    cmd->input = cmd->output = NULL;
    cmd->next = NULL;
    //初始化相应变量
    for(i = begin; i<10; i++){
        buff[i][0] = '\0';
    }
    inputFile[0] = '\0';
    outputFile[0] = '\0';
    
    i = begin;

	//跳过空格等无用信息
    while(i < end && (inputBuff[i] == ' ' || inputBuff[i] == '\t')){

        i++;
    }

    k = 0;
    j = 0;
    fileFinished = 0;
	pipeCmdFinished = 0;
	//buff存的是命令的各个参数
    temp = buff[k]; //以下通过temp指针的移动实现对buff[i]的顺次赋值过程
	#ifdef DEBUG_LJL_PIPE
		printf("value of i and end: %d  %d\ninputBuff: ",i,end);
	#endif
    while(i < end && !pipeCmdFinished){
		#ifdef DEBUG_LJL_PIPE
			printf("%c",inputBuff[i]);
		#endif
		/*根据命令字符的不同情况进行不同的处理*/
        switch(inputBuff[i]){
            case ' ':
            case '\t': //命令名及参数的结束标志
                temp[j] = '\0';
				#ifdef DEBUG_LJL_PIPE
					printf("%s\n",temp);
				#endif
                j = 0;
                if(!fileFinished){
                    k++;
                    temp = buff[k];
                }
                break;

			//不明白的部分
            case '<': //输入重定向标志
				//printf("In the case '<'\n");
                if(j != 0){
		  		  	//此判断为防止命令直接挨着<符号导致判断为同一个参数，如果ls<sth
					//这种情况就是命令后紧接着<字符                    
					temp[j] = '\0';
                    j = 0;
                    if(!fileFinished){
                        k++;
                        temp = buff[k];
                    }
                }
                temp = inputFile;
				#ifdef DEBUG_LJL_PIPE
					printf("%s\n",temp);
				#endif
                fileFinished = 1;
                i++;
                break;
                
            case '>': //输出重定向标志
                if(j != 0){
				//命令后紧接着>符号的情况
                    temp[j] = '\0';
                    j = 0;
                    if(!fileFinished){
                        k++;
                        temp = buff[k];
                    }
                }
                temp = outputFile;
				#ifdef DEBUG_LJL_PIPE
					printf("%s\n",temp);
				#endif
                fileFinished = 1;
                i++;
                break;
                
            case '&': //后台运行标志，实现方法保证了无论&符号在命令前还是后，都能正确执行
                if(j != 0){
                    temp[j] = '\0';
					#ifdef DEBUG_LJL_PIPE
						printf("%s\n",temp);
					#endif
                    j = 0;
                    if(!fileFinished){
                        k++;
                        temp = buff[k];
                    }
                }
                cmd->isBack = 1;
                fileFinished = 1;
                i++;
                break;

	 		case '|'://管道指令的标志
					if(j!=0){
						#ifdef DEBUG_LJL_PIPE
							printf("when '|',temp[j-1]=%c\n",temp[j-1]);
							printf("inputFile[0]=%c\n",inputFile[0]);
						#endif
						temp[j]='\0';
						j=0;
						if(!fileFinished){
							k++;
							temp=buff[k];
						}
					}
					i++;
					#ifdef DEBUG_LJL_PIPE
						printf("pipe: %d\n",i);
					#endif
					cmd->next=handleSimpleCmdStr(i, end);

					#ifdef DEBUG_LJL_PIPE
						testCmd = cmd;					
//						if(cmd != NULL)
//							printf("PipeCmd : %s",cmd->args[0]);
//						printf("%d\n",cmd);
//						getchar();

					#endif
					fileFinished=1;
					pipeCmdFinished=1;
					break;

            default: //默认则读入到temp指定的空间
                temp[j++] = inputBuff[i++];
                continue;
		}
        
		//跳过空格等无用信息
        while(i < end && (inputBuff[i] == ' ' || inputBuff[i] == '\t')){
            i++;
        }
	}
	#ifdef DEBUG_LJL_PIPE
		printf("\n");
	#endif
    
	//这是在处理什么情况呢？
    if(inputBuff[end-1] != ' ' && inputBuff[end-1] != '\t' && inputBuff[end-1] != '&'){
        temp[j] = '\0';
        if(!fileFinished){
            k++;
        }
    }
    
	//依次为命令名及其各个参数赋值
    cmd->args = (char**)malloc(sizeof(char*) * (k + 1));
    cmd->args[k] = NULL;
    for(i = 0; i<k; i++){
        j = strlen(buff[i]);
        cmd->args[i] = (char*)malloc(sizeof(char) * (j + 1));   
        strcpy(cmd->args[i], buff[i]);
    }
    
	//如果有输入重定向文件，则为命令的输入重定向变量赋值
    if(strlen(inputFile) != 0){
        j = strlen(inputFile);
		//printf("cnmb\n");
        cmd->input = (char*)malloc(sizeof(char) * (j + 1));
        strcpy(cmd->input, inputFile);
    }

    //如果有输出重定向文件，则为命令的输出重定向变量赋值
    if(strlen(outputFile) != 0){
        j = strlen(outputFile);
        cmd->output = (char*)malloc(sizeof(char) * (j + 1));   
        strcpy(cmd->output, outputFile);
    }
    #ifdef DEBUG
    printf("****\n");
    printf("isBack: %d\n",cmd->isBack);
    for(i = 0; cmd->args[i] != NULL; i++){
    	printf("args[%d]: %s\n",i,cmd->args[i]);
	}
    printf("input: %s\n",cmd->input);
    printf("output: %s\n",cmd->output);
	printf("next: %d\n",cmd->next);
    printf("****\n");
    #endif
	#ifdef DEBUG_LJL_PIPE
		printf("cmd: %d\n",cmd);
	#endif
    
    return cmd;
}

/*******************************************************
                      命令执行
********************************************************/
/*执行外部命令*/
void execOuterCmd(SimpleCmd *cmd){
    pid_t pid;
    int pipeIn, pipeOut;
   
	#ifdef DEBUG_LJL
		Job *ptrJob;
		printf("In the function execOuterCmd\n");
	#endif
    if(exists(cmd->args[0])){ //命令存在
		pid = fork();
        #ifdef DEBUG_LJL
			printf("pid of this is %d\n",getpid());
		#endif
        if(pid < 0){
            perror("fork failed");
            return;
        }

        if(pid == 0){ //子进程
			#ifdef DEBUG_LJL
				printf("In the child\n");
			#endif
            if(cmd->input != NULL){ //存在输入重定向
				//S_IRUSR权限,代表该文件所有者具有可读取的权限.
				//S_IWUSR权限,代表该文件所有者具有可写入的权限.为什么输入文件需要写入权限？
                if((pipeIn = open(cmd->input, O_RDONLY, S_IRUSR|S_IWUSR)) == -1){
                    printf("不能打开文件 %s！\n", cmd->input);
                    return;
                }
                if(dup2(pipeIn, 0) == -1){
                    printf("重定向标准输入错误！\n");
                    return;
                }
            }
            
            if(cmd->output != NULL){ //存在输出重定向
                if((pipeOut = open(cmd->output, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1){
                    printf("不能打开文件 %s！\n", cmd->output);
                    return ;
                }
                if(dup2(pipeOut, 1) == -1){
                    printf("重定向标准输出错误！\n");
                    return;
                }
            }
            //这一段代码不理解
            if(cmd->isBack){ //若是后台运行命令，等待父进程增加作业
				#ifdef DEBUG_LJL
				printf("Into the if-statement\n");
				#endif
                signal(SIGUSR1, setGoon); //收到信号，setGoon函数将goon置1，以跳出下面的循环
				while(goon == 0); //等待父进程SIGUSR1信号，表示作业已加到链表中
				#ifdef DEBUG_LJL
				printf("Out of the child's loop\n");
				#endif
                goon = 0; //置0，为下一命令做准备
                
                printf("[%d]\t%s\t\t%s\n", getpid(), RUNNING, inputBuff);
                kill(getppid(), SIGUSR1);//这里的函数是getppid()，是取得父进程的进程id，即这句是向父进程发送信号
				#ifdef DEBUG_LJL
				printf("Signal has been sent to father from child\n");
				#endif
            }


            justArgs(cmd->args[0]);//修改第一个参数的格式，去掉'/'
			#ifdef DEBUG_LJL
				printf("cmdBuff is %s\n",cmdBuff);
			#endif
            if(execv(cmdBuff, cmd->args) < 0){ //执行命令
                printf("execv failed!\n");
                return;
            }
        }
		else{ //父进程
		#ifdef DEBUG_LJL
			printf("In the father\n");
		#endif
            if(cmd ->isBack){ //后台命令      
                fgPid = 0; //fgPid置0，为下一命令做准备
				#ifdef DEBUG_LJL
					printf("The pid of child is %d\n",pid);
				#endif
                addJob(pid); //增加新的作业
				//为什么有了这一个输出之后，子进程就运行了?
				//但是父进程没有发送信号
				#ifdef DEBUG_LJL
					ptrJob = head;
					while(ptrJob != NULL){
						printf("Jobs: %s\n",ptrJob->cmd);
						ptrJob = ptrJob->next;		
					}
				#endif
                kill(pid, SIGUSR1); //向子进程发信号，表示作业已加入
				#ifdef DEBUG_LJL
				printf("Signal has been sent to child(pid:%d) from father\n",pid);
				#endif				
                
                //等待子进程输出
                signal(SIGUSR1, setGoon);
                while(goon == 0) ;
				#ifdef DEBUG_LJL
				printf("Out of the father's loop\n");
				#endif
                goon = 0;
            }else{ //非后台命令
                fgPid = pid;
                waitpid(pid, NULL, 0);
				fgPid = 0;
            }
		}
    }else{ //命令不存在
        printf("找不到命令 15%s\n", inputBuff);
    }
}

/*执行命令*/
void execSimpleCmd(SimpleCmd *cmd){
    int i, pid;
	int pipeIn,pipeOut;
    char *temp;
    Job *now = NULL;
	glob_t globbuf;

//	printf("cmd->args[0] is: %s\n",cmd->args[0]);
	if(strcmp(cmd->args[0], "exit") == 0) { //exit命令
        exit(0);
    } else if (strcmp(cmd->args[0], "history") == 0) { //history命令
        if(history.end == -1){
            printf("尚未执行任何命令\n");
			dup2(pipeOut,1);
            return;
        }
        i = history.start;
        do {
            printf("%s\n", history.cmds[i]);
            i = (i + 1)%HISTORY_LEN;
        } while(i != (history.end + 1)%HISTORY_LEN);
    } else if (strcmp(cmd->args[0], "jobs") == 0) { //jobs命令
        if(head == NULL){
            printf("尚无任何作业\n");
        } else {
            printf("index\tpid\tstate\t\tcommand\n");
            for(i = 1, now = head; now != NULL; now = now->next, i++){
                printf("%d\t%d\t%s\t\t%s\n", i, now->pid, now->state, now->cmd);
            }
        }
    } else if (strcmp(cmd->args[0], "cd") == 0) {
        temp = cmd->args[1];
        if(temp != NULL){
            if(chdir(temp) < 0){
                printf("cd; %s 错误的文件名或文件夹名！\n", temp);
            }
        }
    } else if (strcmp(cmd->args[0], "fg") == 0) { //fg命令
        temp = cmd->args[1];
        if(temp != NULL && temp[0] == '%'){
            pid = str2Pid(temp, 1, strlen(temp));
            if(pid != -1){
                fg_exec(pid);
            }
        }else{
            printf("fg; 参数不合法，正确格式为：fg %%<int>\n");
        }
    } else if (strcmp(cmd->args[0], "bg") == 0) { //bg命令
        temp = cmd->args[1];
        if(temp != NULL && temp[0] == '%'){
            pid = str2Pid(temp, 1, strlen(temp));
            
            if(pid != -1){
                bg_exec(pid);
            }
        }
		else{
            printf("bg; 参数不合法，正确格式为：bg %%<int>\n");
        }
    } else{ //外部命令
		#ifdef DEBUG_LJL
			printf("Outer Cmd\n");
		#endif
		//实现通配符
		//参数列表中是否存在通配符
		//printf("%s\n",cmd->args[0]);
		i = 0;
		while(cmd->args[i] != NULL)
		{
			#ifdef DEBUG_LJL_GLOB
				printf("In the Loop,time: %d\n",i);
			#endif
			if(globExist(cmd->args[i]))
			{
			//当前参数存在通配符
			#ifdef DEBUG_LJL_GLOB
				printf("globExitst is true\n");
				getchar();			
			#endif
				//查找通配符的匹配
				if(glob(cmd->args[i],GLOB_NOCHECK,NULL,&globbuf) == 0)
				{
				//成功匹配
					//用匹配到的文件名替换原来含有通配符的表达式
					i = replace(cmd,i,globbuf.gl_pathc,globbuf.gl_pathv);
					#ifdef DEBUG_LJL_GLOB
						//调试用，pid在此处仅为临时变量
						pid = 0;
						while(cmd->args[pid] != NULL)
						{
							printf("New arguements: %d  %s\n",pid,cmd->args[pid]);
							pid++;
						}						
					#endif
					globfree(&globbuf);
				}
			}
			i++;
		}
        execOuterCmd(cmd);
    }

    //释放结构体空间
    for(i = 0; cmd->args[i] != NULL; i++){
        free(cmd->args[i]);
    }
	free(cmd->input);
    free(cmd->output);
}

//管道
int execPipeCmd(SimpleCmd *cmd1,SimpleCmd *cmd2){
	int status;
	int pid[2];
	int pipe_fd[2];
	
	//创建管道
	if(pipe(pipe_fd)<0){
		perror("pipe failed");
		return -1;
	}
	unlink(FIFO);
	mkfifo(FIFO,0666);
	//为cmd1创建进程
	if((pid[0]=fork())<0){
		perror("fork failed");
		return -1;
	}
	if(!pid[0]){
		/*子进程*/
		#ifdef DEBUG_LJL_PIPE
			printf("In the cmd1\n");
		#endif
		
		/*将管道的写描述符复制给标准输出，然后关闭*/
		int out = open(FIFO,O_WRONLY);
			if(cmd1->input != NULL)
				{
					int in = open(cmd1->input,O_RDONLY,S_IREAD|S_IWRITE);
					printf("cmd1->input is not NULL\n");
					dup2(in,STDIN_FILENO);
				}
		dup2(out,STDOUT_FILENO);
		/*执行cmd1*/
		execSimpleCmd(cmd1);
		exit(0);
	}
	if(pid[0]){
		/*父进程*/
		//waitpid(pid[0],&status,0);
		/*为cmd2创建子进程*/
		if((pid[1]=fork())<0){
			perror("fork failed");
			return -1;
		}
		if(!pid[1]){
			/*子进程*/
			#ifdef DEBUG_LJL_PIPE
				printf("In the cmd2\n");
			#endif
		
			/*将管道的读描述符复制给标准输入*/
			
			/*执行cmd2*/
//			printf("execute cmd2\n");
			int in = open(FIFO,O_RDONLY);

				dup2(in,STDIN_FILENO);
			
			execSimpleCmd(cmd2);
			exit(0);
		}
		waitpid(pid[0],&status,0);
		waitpid(pid[1],&status,0);
	}
	return 0;
}

/*******************************************************
                     通配符处理
********************************************************/
//检查字符串中是否存在通配符
int globExist(char *arg)
{
	#ifdef DEBUG_LJL_GLOB
		printf("In the globExist function,string: %s\n",arg);
	#endif
	int i = 0;

	if(arg[0] == '-')
	{
	//是命令参数
		return 0;
	}

	while(arg[i] != '\0')
	{
		if(arg[i] == '*' || arg[i] == '?')
			return 1;
		i++;
	}

	return 0;
}

//字符串替换函数，用于用匹配的文件名替换含有通配符的表达式
//返回修改后匹配的最后文件名所在的位置
int replace(SimpleCmd *cmd,int i,int gl_pathc,char **gl_pathv)

{
	int tmp = 0,_tmp = 0;
	int length = 0;
	char **merge = NULL;

	//获得args数组本身的长度
	while(cmd->args[length] != NULL)
	{
		length++;
	}
	#ifdef DEBUG_LJL_GLOB
		printf("value of length: %d\n",length);
		printf("value of gl_pathc: %d\n",gl_pathc);		
	#endif
	//为合并后的数组分配空间
	merge = (char **)malloc(sizeof(char*) * (length + gl_pathc+1));

	#ifdef DEBUG_LJL_GLOB
		printf("Former length: %d\n",i);
	#endif
	//拷贝含有通配符的表达式之前的参数
	while(tmp < i)
	{
	#ifdef DEBUG_LJL_GLOB
		printf("Loop %d: Former arguements %d has been copied from %s to ",tmp,tmp,cmd->args[tmp]);
	#endif
		merge[tmp] = (char *)malloc(sizeof(char) * (strlen(cmd->args[tmp]) + 1));
		//merge[tmp][strlen(cmd->args[tmp])] = 0;
		strcpy(merge[tmp],cmd->args[tmp]);
		merge[tmp][strlen(cmd->args[tmp])] = 0;
		//merge[tmp] = cmd->args[tmp];		
		//free(cmd->args[tmp]);
		
	#ifdef DEBUG_LJL_GLOB
		printf("%s\n",merge[tmp]);
		printf("length of cmd->args[%d] and merge[%d]: %d  %d\n",tmp,tmp,strlen(cmd->args[tmp]),strlen(merge[tmp]));
	#endif
	tmp++;
	}

	_tmp = 0;
	#ifdef DEBUG_LJL_GLOB
		tmp = i;
	#endif

	#ifdef DEBUG_LJL_GLOB
		printf("Replacing length: %d\n",gl_pathc);
	#endif
	//printf("%d\n",tmp);
	//printf("%s\n",merge[0]);
	//拷贝匹配通配符表达式的文件名
	while(_tmp < gl_pathc)
	{
	#ifdef DEBUG_LJL_GLOB
		printf("Replacing: %d %s\n",tmp,gl_pathv[_tmp]);
	#endif
		merge[tmp] = (char *)malloc(sizeof(char) * (strlen(gl_pathv[_tmp]) + 1));
		strcpy(merge[tmp],gl_pathv[_tmp]);
		_tmp++;
		tmp++;
	}

	#ifdef DEBUG_LJL_GLOB
		printf("later length: %d\n",length);
	#endif
	//printf("%s\n",merge[0]);
	_tmp = i;//记录下含有通配符的表达式的位置以供返回
	i++;//跳过含有通配符的表达式
	#ifdef DEBUG_LJL_GLOB
		printf("value of i: %d\n",i);
	#endif
	//拷贝剩下还未处理的参数
	while(i < length)
	{
	#ifdef DEBUG_LJL_GLOB
		printf("Later arguements: %d\n",tmp) ;
	#endif
		merge[tmp] = (char *)malloc(sizeof(char) * (strlen(cmd->args[i]) + 1));
		merge[tmp][strlen(cmd->args[i])+1] = 0;
		strcpy(merge[tmp],cmd->args[i]);
		merge[tmp][strlen(cmd->args[i])+1] = 0;
		//free(cmd->args[i]);
		i++;
		tmp++;
	}
	merge[tmp] = NULL;

	#ifdef DEBUG_LJL_GLOB
		getchar();
	#endif
	//free(cmd->args);
	#ifdef DEBUG_LJL_GLOB
		getchar();
	#endif
	//cmd->args = merge;
	for (i = 0; merge[i] != NULL; i++)
	{
		cmd->args[i] = malloc(sizeof(char) * (strlen(merge[i])+1));
		cmd->args[i][strlen(merge[i])] = 0;
		strcpy(cmd->args[i],merge[i]);
		cmd->args[i][strlen(merge[i])] = 0;
	}
	#ifdef DEBUG_LJL_GLOB
		getchar();
	#endif
	#ifdef DEBUG_LJL_GLOB
		i = 0;
		while(merge[i] != NULL)
		{
			printf("In the last loop,time: %d\n",i);
			printf("Replaced args[%d]: %s\n",i,cmd->args[i]);
			i++;
		}
	#endif
	return _tmp;

}

/*******************************************************
                     命令执行接口
********************************************************/
void execute(){
    SimpleCmd *cmd = handleSimpleCmdStr(0, strlen(inputBuff));

	if(cmd->next!=NULL){
	//是管道指令
	#ifdef DEBUG_LJL_PIPE
		printf("Pipe Cmd \n");
	#endif
		if(execPipeCmd(cmd,cmd->next)){
			printf("Error occurs when execute pipe command\n");
			return ;
		}
	}
	else{
	//不是管道指令
	#ifdef DEBUG_LJL_PIPE
		printf("SimpleCmd\n");
	#endif
		execSimpleCmd(cmd);
	}
}
/*
void execute_pipe()
{
	SimpleCmd *cmd = handleSimpleCmdStr(0,strlen(inputBuff));



}
*/
