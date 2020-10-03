/*****************************************************************************
    NUTS version 3.3.3 (Triple Three :) - Copyright (C) Neil Robertson 1996
                     Last update: 18th November 1996

 This software is provided as is. It is not intended as any sort of bullet
 proof system for commercial operation (though you may use it to set up a 
 pay-to-use system, just don't attempt to sell the code itself) and I accept 
 no liability for any problems that may arise from you using it. Since this is 
 freeware (NOT public domain , I have not relinquished the copyright) you may 
 distribute it as you see fit and you may alter the code to suit your needs.

 Read the COPYRIGHT file for further information.

 Neil Robertson. 
 Email    : neil@ogham.demon.co.uk
 Home page: http://www.wso.co.uk/neil.html (need JavaScript enabled browser)
 NUTS page: http://www.wso.co.uk/nuts.html
 Newsgroup: alt.talkers.nuts

 NB: This program listing looks best when the tab length is 5 chars which is
 "set ts=5" in vi.

 *****************************************************************************/

#include <stdio.h>
#ifdef _AIX
#include <sys/select.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <setjmp.h>
#include <errno.h>

#include "nuts427.h"

#define VERSION "3.3.3"

/*** This function calls all the setup routines and also contains the
	main program loop ***/
main(argc,argv)
int argc;
char *argv[];
{
fd_set readmask; 
int i,len,counter;
char inpstr[ARR_SIZE];
char *remove_first();
UR_OBJECT user,next;
NL_OBJECT nl;

strcpy(progname,argv[0]);
if (argc<2) strcpy(confile,CONFIGFILE);
else strcpy(confile,argv[1]);

/* Startup */
printf("\n*** NUTS %s server booting ***\n\n",VERSION);
init_globals();
write_syslog("\n*** SERVER BOOTING ***\n",0,TOSYS);
set_date_time();
init_signals();
load_and_parse_config();
init_sockets();
if (auto_connect) init_connections();
else printf("Skipping connect stage.\n");
check_messages(NULL,1);

/* Run in background automatically. */
switch(fork()) {
	case -1: boot_exit(11);  /* fork failure */
	case  0: break; /* child continues */
	default: sleep(1); exit(0);  /* parent dies */
	}
reset_alarm();
printf("\n*** Booted with PID %d ***\n\n",getpid());
sprintf(text,"*** Booted successfully with PID %d %s ***\n\n",getpid(),long_date(1));
write_syslog(text,0,TOSYS);

/**** Main program loop. *****/
setjmp(jmpvar); /* jump to here if we crash and crash_action = IGNORE */
while(1) {

	/* set up mask then wait */
	setup_readmask(&readmask);
	if (select(FD_SETSIZE,&readmask,0,0,0)==-1) continue;

	/* check for connection to listen sockets */
	for(i=0;i<3;++i) {
		if (FD_ISSET(listen_sock[i],&readmask)) 
			accept_connection(listen_sock[i],i);
		}

	/* Cycle through client-server connections to other talkers */

/* ATTENZIONE : questa aggiunta deve essere attentamente  */
/* controllata... dovrebbe funzionare ma non e' detto che */
/* non salti fuori un bug improvviso...                   */

	clear_words();

/**********************************************************/


	for(nl=nl_first;nl!=NULL;nl=nl->next) {
		no_prompt=0;
		if (nl->type==UNCONNECTED || !FD_ISSET(nl->socket,&readmask)) 
			continue;
		/* See if remote site has disconnected */
		if (!(len=read(nl->socket,inpstr,sizeof(inpstr)-3))) {
			if (nl->stage==UP)
				sprintf(text,"NETLINK: Remote disconnect by %s.\n",nl->service);
			else sprintf(text,"NETLINK: Remote disconnect by site %s.\n",nl->site);
			write_syslog(text,1,TOSYS);
			sprintf(text,"~OLSYSTEM:~RS Lost link to %s in the %s.\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			shutdown_netlink(nl);
			continue;
			}
		inpstr[len]='\0'; 

/****************************************************


   punto di controllo per l'ingresso di netlink...

     decommentare per utilizzare

		if (user_first!=NULL) {
			write_user(user_first,inpstr);
			sprintf(text,"0.%s\n 1.%s\n 2.%s\n 3.%s\n",word[0],word[1],word[2],word[3]);
			write_user(user_first,text);
			}

************************************************/
		exec_netcom(nl,inpstr);
		}

	/* Cycle through users. Use a while loop instead of a for because
	    user structure may be destructed during loop in which case we
	    may lose the user->next link. */
	user=user_first;
	while(user!=NULL) {
		next=user->next; /* store in case user object is destructed */
		/* If remote user or clone ignore */
		if (user->type!=USER_TYPE) {  user=next;  continue; }

		/* see if any data on socket else continue */
		if (!FD_ISSET(user->socket,&readmask)) { user=next;  continue; }
	
		/* see if client (eg telnet) has closed socket */
		inpstr[0]='\0';
		if (!(len=read(user->socket,inpstr,sizeof(inpstr)))) {
			disconnect_user(user);  user=next;
			continue;
			}
		/* ignore control code replies */
		if ((unsigned char)inpstr[0]==255) { user=next;  continue; }

		/* Deal with input chars. If the following if test succeeds we
		   are dealing with a character mode client so call function. */
		if (inpstr[len-1]>=32 || user->buffpos) {
			if (get_charclient_line(user,inpstr,len)) goto GOT_LINE;
			user=next;  continue;
			}
		else terminate(inpstr);

		GOT_LINE:
		no_prompt=0;  
		com_num=-1;
		force_listen=0; 
		destructed=0;
		user->buff[0]='\0';  
		user->buffpos=0;
		user->last_input=time(0);
		if (user->login) {
			login(user,inpstr);  user=next;  continue;  
			}

		/* If a dot on its own then execute last inpstr unless its a misc
		   op or the user is on a remote site */
		if (!user->misc_op) {
			if (!strcmp(inpstr,".") && user->inpstr_old[0]) {
				strcpy(inpstr,user->inpstr_old);
				sprintf(text,"%s\n",inpstr);
				write_user(user,text);
				}
			/* else save current one for next time */
			else {
				if (inpstr[0]) strncpy(user->inpstr_old,inpstr,REVIEW_LEN);
				}
			}

		/* Main input check */
		clear_words();
		word_count=wordfind(inpstr);
		if (user->afk) {
			if (user->afk==2) {
				if (!word_count) {  
					if (user->command_mode) prompt(user);
					user=next;  continue;  
					}
				if (strcmp((char *)crypt(word[0],"NU"),user->pass)) {
					write_user(user,"Incorrect password.\n"); 
					prompt(user);  user=next;  continue;
					}
				cls(user);
				write_user(user,"Session unlocked, you are no longer AFK.\n");
				}	
			else write_user(user,"You are no longer AFK.\n");  
			user->afk_mesg[0]='\0';
			if (user->vis) {
				sprintf(text,"%s comes back from being AFK.\n",user->name);
				write_room_except(user->room,text,user);
				}
			if (user->afk==2) {
				user->afk=0;  prompt(user);  user=next;  continue;
				}
			user->afk=0;
			}
		if (!word_count) {
			if (misc_ops(user,inpstr))  {  user=next;  continue;  }
			if (user->room==NULL) {
				sprintf(text,"ACT %s NL\n",user->name);
				write_sock(user->netlink->socket,text);
				}
			if (user->command_mode) prompt(user);
			user=next;  continue;
			}
		if (misc_ops(user,inpstr))  {  user=next;  continue;  }
		com_num=-1;
		if (user->command_mode || strchr(".;!<>-#+",inpstr[0])) 
			exec_com(user,inpstr);
		else say(user,inpstr);
		if (!destructed) {
			if (user->room!=NULL)  prompt(user); 
			else {
				switch(com_num) {
					case -1  : /* Unknown command */
					case HOME:
					case QUIT:
					case MODE:
					case PROMPT: 
					case SUICIDE:
					case REBOOT:
					case SHUTDOWN: prompt(user);
					}
				}
			}
		user=next;
		}
	} /* end while */
}


/************ MAIN LOOP FUNCTIONS ************/

/*** Set up readmask for select ***/
setup_readmask(mask)
fd_set *mask;
{
UR_OBJECT user;
NL_OBJECT nl;
int i;

FD_ZERO(mask);
for(i=0;i<3;++i) FD_SET(listen_sock[i],mask);
/* Do users */
for (user=user_first;user!=NULL;user=user->next) 
	if (user->type==USER_TYPE) FD_SET(user->socket,mask);

/* Do client-server stuff */
for(nl=nl_first;nl!=NULL;nl=nl->next) 
	if (nl->type!=UNCONNECTED) FD_SET(nl->socket,mask);
}


/*** Accept incoming connections on listen sockets ***/
accept_connection(lsock,num)
int lsock,num;
{
UR_OBJECT user,create_user();
NL_OBJECT create_netlink();
char *get_ip_address(),site[80];
struct sockaddr_in acc_addr;
int accept_sock,size;

size=sizeof(struct sockaddr_in);
accept_sock=accept(lsock,(struct sockaddr *)&acc_addr,&size);
if (num==2) {
	accept_server_connection(accept_sock,acc_addr);  return;
	}
strcpy(site,get_ip_address(acc_addr));
if (site_banned(site)) {
	write_sock(accept_sock,"\n\rLogins from your site/domain are banned.\n\n\r");
	close(accept_sock);
	sprintf(text,"Attempted login from banned site %s.\n",site);
	write_syslog(text,1,TOSYS);
	return;
	}

sprintf(text,"Login initiated from %s\n",site);
write_syslog(text,1,TOSYS);

more(NULL,accept_sock,MOTD1); /* send pre-login message */
if (num_of_users+num_of_logins>=max_users && !num) {
	write_sock(accept_sock,"\n\rSorry, the talker is full at the moment.\n\n\r");
	close(accept_sock);  
	return;
	}
if ((user=create_user())==NULL) {
	sprintf(text,"\n\r%s: unable to create session.\n\n\r",syserror);
	write_sock(accept_sock,text);
	close(accept_sock);  
	return;
	}
user->socket=accept_sock;
user->login=4;
user->last_input=time(0);
if (!num) user->port=port[0]; 
else {
	user->port=port[1];
	write_user(user,"** Wizport login **\n\n");
	}
strcpy(user->site,site);
user->site_port=(int)ntohs(acc_addr.sin_port);
echo_on(user);
write_user(user,"Give me a name: ");
num_of_logins++;
}


/*** Get net address of accepted connection ***/
char *get_ip_address(acc_addr)
struct sockaddr_in acc_addr;
{
static char site[80];
struct hostent *host;

strcpy(site,(char *)inet_ntoa(acc_addr.sin_addr)); /* get number addr */
if ((host=gethostbyaddr((char *)&acc_addr.sin_addr,4,AF_INET))!=NULL)
	strcpy(site,host->h_name); /* copy name addr. */
strtolower(site);
return site;
}


/*** See if users site is banned ***/
site_banned(site)
char *site;
{
FILE *fp;
char line[82],filename[80];

sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%s",line);
while(!feof(fp)) {
	if (strstr(site,line)) {  fclose(fp);  return 1;  }
	fscanf(fp,"%s",line);
	}
fclose(fp);
return 0;
}


/*** See if user is banned ***/
user_banned(name)
char *name;
{
FILE *fp;
char line[82],filename[80];

sprintf(filename,"%s/%s",DATAFILES,USERBAN);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%s",line);
while(!feof(fp)) {
	if (!strcmp(line,name)) {  fclose(fp);  return 1;  }
	fscanf(fp,"%s",line);
	}
fclose(fp);
return 0;
}


/*** Attempt to get '\n' terminated line of input from a character
     mode client else store data read so far in user buffer. ***/
get_charclient_line(user,inpstr,len)
UR_OBJECT user;
char *inpstr;
int len;
{
int l;

for(l=0;l<len;++l) {
	/* see if delete entered */
	if (inpstr[l]==8 || inpstr[l]==127) {
		if (user->buffpos) {
			user->buffpos--;
			if (user->charmode_echo) write_user(user,"\b \b");
			}
		continue;
		}
	user->buff[user->buffpos]=inpstr[l];
	/* See if end of line */
	if (inpstr[l]<32 || user->buffpos+2==ARR_SIZE) {
		terminate(user->buff);
		strcpy(inpstr,user->buff);
		if (user->charmode_echo) write_user(user,"\n");
		return 1;
		}
	user->buffpos++;
	}
if (user->charmode_echo
    && ((user->login!=3 && user->login!=2) || password_echo)) 
	write(user->socket,inpstr,len);
return 0;
}


/*** Put string terminate char. at first char < 32 ***/
terminate(str)
char *str;
{
int i;
for (i=0;i<ARR_SIZE;++i)  {
	if (*(str+i)<32) {  *(str+i)=0;  return;  } 
	}
str[i-1]=0;
}


/*** Get words from sentence. This function prevents the words in the 
     sentence from writing off the end of a word array element. This is
     difficult to do with sscanf() hence I use this function instead. ***/
wordfind(inpstr)
char *inpstr;
{
int wn,wpos;

wn=0; wpos=0;
do {
	while(*inpstr<33) if (!*inpstr++) return wn;
	while(*inpstr>32 && wpos<WORD_LEN-1) {
		word[wn][wpos]=*inpstr++;  wpos++;
		}
	word[wn][wpos]='\0';
	wn++;  wpos=0;
	} while (wn<MAX_WORDS);
return wn-1;
}


/*** clear word array etc. ***/
clear_words()
{
int w;
for(w=0;w<MAX_WORDS;++w) word[w][0]='\0';
word_count=0;
}


/************ PARSE CONFIG FILE **************/

load_and_parse_config()
{
FILE *fp;
int loadspc=ROOM_NAME_LEN+MAX_LINKS*(ROOM_NAME_LEN+1)+MAPTYPE_LEN+50;
char line[ROOM_NAME_LEN+MAX_LINKS*(ROOM_NAME_LEN+1)+MAPTYPE_LEN+50];
char c,filename[80];
int i,section_in,got_init,got_rooms;
RM_OBJECT rm1,rm2;
NL_OBJECT nl;

section_in=0;
got_init=0;
got_rooms=0;

sprintf(filename,"%s/%s",DATAFILES,confile);
printf("Parsing config file \"%s\"...\n",filename);
if (!(fp=fopen(filename,"r"))) {
	perror("NUTS: Can't open config file");  boot_exit(1);
	}
/* Main reading loop */
config_line=0;
fgets(line,loadspc,fp);
while(!feof(fp)) {
	config_line++;
	for(i=0;i<8;++i) wrd[i][0]='\0';
	sscanf(line,"%s %s %s %s %s %s %s %s",wrd[0],wrd[1],wrd[2],wrd[3],wrd[4],wrd[5],wrd[6],wrd[7]);
	if (wrd[0][0]=='#' || wrd[0][0]=='\0') {
		fgets(line,loadspc,fp);  continue;
		}
	/* See if new section */
	if (wrd[0][strlen(wrd[0])-1]==':') {
		if (!strcmp(wrd[0],"INIT:")) section_in=1;
			else if (!strcmp(wrd[0],"SITES:")) section_in=3; 
				else {
					fprintf(stderr,"NUTS: Unknown section header on line %d.\n",config_line);
					fclose(fp);  boot_exit(1);
					}
		}
	switch(section_in) {
		case 1: parse_init_section();  got_init=1;  break;
		case 3: parse_sites_section(); break;
		default:
			fprintf(stderr,"NUTS: Section header expected on line %d.\n",config_line);
			boot_exit(1);
		}
	fgets(line,loadspc,fp);
	}
fclose(fp);


/* parsing room file */

config_line=0;
sprintf(filename,"%s/%s",DATAFILES,ROOMCONFIG);

if (!(fp=fopen(filename,"r"))) {
        perror("NUTS: can't open room config file"); boot_exit(1);
        }
fgets(line,loadspc,fp);

while (!feof(fp)) {
	config_line++;
        for(i=0;i<8;++i) wrd[i][0]='\0';
        sscanf(line,"%s %s %s %s %s %s %s %s",wrd[0],wrd[1],wrd[2],wrd[3],wrd[4],wrd[5],wrd[6],wrd[7]);
        if (wrd[0][0]=='\0') {
                fgets(line,loadspc,fp);  continue;
                }
        parse_rooms_section(); got_rooms=1;
        fgets(line,loadspc,fp);
        }

fclose(fp);

/* open shout channel */ 

open_shout_channel();



/* See if required sections were present (SITES is optional) and if
   required parameters were set. */
if (!got_init) {
	fprintf(stderr,"NUTS: INIT section missing from config file.\n");
	boot_exit(1);
	}
if (!got_rooms) {
	fprintf(stderr,"NUTS: ROOMS section missing from config file.\n");
	boot_exit(1);
	}
if (!verification[0]) {
	fprintf(stderr,"NUTS: Verification not set in config file.\n");
	boot_exit(1);
	}
if (!port[0]) {
	fprintf(stderr,"NUTS: Main port number not set in config file.\n");
	boot_exit(1);
	}
if (!port[1]) {
	fprintf(stderr,"NUTS: Wiz port number not set in config file.\n");
	boot_exit(1);
	}
if (!port[2]) {
	fprintf(stderr,"NUTS: Link port number not set in config file.\n");
	boot_exit(1);
	}
if (port[0]==port[1] || port[1]==port[2] || port[0]==port[2]) {
	fprintf(stderr,"NUTS: Port numbers must be unique.\n");
	boot_exit(1);
	}
if (room_first==NULL) {
	fprintf(stderr,"NUTS: No rooms configured in config file.\n");
	boot_exit(1);
	}

/* Parsing done, now check data is valid. Check room stuff first. */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
	for(i=0;i<MAX_LINKS;++i) {
		if (!rm1->link_label[i][0]) break;
		for(rm2=room_first;rm2!=NULL;rm2=rm2->next) {
			if (rm1==rm2) continue;
			if (!strcmp(rm1->link_label[i],rm2->label)) {
				rm1->link[i]=rm2;  break;
				}
			}
		if (rm1->link[i]==NULL && strcmp(rm1->link_label[i],"null")) {
			fprintf(stderr,"NUTS: Room %s has undefined link label '%s'.\n",rm1->name,rm1->link_label[i]);
			boot_exit(1);
			}
		}
	}

/* Check external links */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
	for(nl=nl_first;nl!=NULL;nl=nl->next) {
		if (!strcmp(nl->service,rm1->name)) {
			fprintf(stderr,"NUTS: Service name %s is also the name of a room.\n",nl->service);
			boot_exit(1);
			}
		if (rm1->netlink_name[0] 
		    && !strcmp(rm1->netlink_name,nl->service)) {
			rm1->netlink=nl;  break;
			}
		}
	if (rm1->netlink_name[0] && rm1->netlink==NULL) {
		fprintf(stderr,"NUTS: Service name %s not defined for room %s.\n",rm1->netlink_name,rm1->name);
		boot_exit(1);
		}
	}

}



/*** Parse init section ***/
parse_init_section()
{
static int in_section=0;
int op,val;
char *options[]={ 
"mainport","wizport","linkport","system_logging","minlogin_level","mesg_life",
"wizport_level","prompt_def","gatecrash_level","min_private","ignore_mp_level",
"rem_user_maxlevel","rem_user_deflevel","verification","mesg_check_time",
"max_users","heartbeat","login_idle_time","user_idle_time","password_echo",
"ignore_sigterm","auto_connect","max_clones","ban_swearing","crash_action",
"colour_def","time_out_afks","allow_caps_in_name","charecho_def",
"time_out_maxlevel","*"
};

if (!strcmp(wrd[0],"INIT:")) { 
	if (++in_section>1) {
		fprintf(stderr,"NUTS: Unexpected INIT section header on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
op=0;
while(strcmp(options[op],wrd[0])) {
	if (options[op][0]=='*') {
		fprintf(stderr,"NUTS: Unknown INIT option on line %d.\n",config_line);
		boot_exit(1);
		}
	++op;
	}
if (!wrd[1][0]) {
	fprintf(stderr,"NUTS: Required parameter missing on line %d.\n",config_line);
	boot_exit(1);
	}
if (wrd[2][0] && wrd[2][0]!='#') {
	fprintf(stderr,"NUTS: Unexpected word following init parameter on line %d.\n",config_line);
	boot_exit(1);
	}
val=atoi(wrd[1]);
switch(op) {
	case 0: /* main port */
	case 1:
	case 2:
	if ((port[op]=val)<1 || val>65535) {
		fprintf(stderr,"NUTS: Illegal port number on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 3:  
	if ((system_logging=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: System_logging must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 4:
	if ((minlogin_level=get_level(wrd[1]))==-1) {
		if (strcmp(wrd[1],"NONE")) {
			fprintf(stderr,"NUTS: Unknown level specifier for minlogin_level on line %d.\n",config_line);
			boot_exit(1);	
			}
		minlogin_level=-1;
		}
	return;

	case 5:  /* message lifetime */
	if ((mesg_life=val)<1) {
		fprintf(stderr,"NUTS: Illegal message lifetime on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 6: /* wizport_level */
	if ((wizport_level=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for wizport_level on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 7: /* prompt defaults */
	if ((prompt_def=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Prompt_def must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 8: /* gatecrash level */
	if ((gatecrash_level=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for gatecrash_level on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 9:
	if (val<1) {
		fprintf(stderr,"NUTS: Number too low for min_private_users on line %d.\n",config_line);
		boot_exit(1);
		}
	min_private_users=val;
	return;

	case 10:
	if ((ignore_mp_level=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for ignore_mp_level on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 11: 
	/* Max level a remote user can remotely log in if he doesn't have a local
	   account. ie if level set to WIZ a GOD can only be a WIZ if logging in 
	   from another server unless he has a local account of level GOD */
	if ((rem_user_maxlevel=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for rem_user_maxlevel on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 12:
	/* Default level of remote user who does not have an account on site and
	   connection is from a server of version 3.3.0 or lower. */
	if ((rem_user_deflevel=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for rem_user_deflevel on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 13:
	if (strlen(wrd[1])>VERIFY_LEN) {
		fprintf(stderr,"NUTS: Verification too long on line %d.\n",config_line);
		boot_exit(1);	
		}
	strcpy(verification,wrd[1]);
	return;

	case 14: /* mesg_check_time */
	if (wrd[1][2]!=':'
	    || strlen(wrd[1])>5
	    || !isdigit(wrd[1][0]) 
	    || !isdigit(wrd[1][1])
	    || !isdigit(wrd[1][3]) 
	    || !isdigit(wrd[1][4])) {
		fprintf(stderr,"NUTS: Invalid message check time on line %d.\n",config_line);
		boot_exit(1);
		}
	sscanf(wrd[1],"%d:%d",&mesg_check_hour,&mesg_check_min);
	if (mesg_check_hour>23 || mesg_check_min>59) {
		fprintf(stderr,"NUTS: Invalid message check time on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 15:
	if ((max_users=val)<1) {
		fprintf(stderr,"NUTS: Invalid value for max_users on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 16:
	if ((heartbeat=val)<1) {
		fprintf(stderr,"NUTS: Invalid value for heartbeat on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 17:
	if ((login_idle_time=val)<10) {
		fprintf(stderr,"NUTS: Invalid value for login_idle_time on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 18:
	if ((user_idle_time=val)<10) {
		fprintf(stderr,"NUTS: Invalid value for user_idle_time on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 19: 
	if ((password_echo=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Password_echo must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 20: 
	if ((ignore_sigterm=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Ignore_sigterm must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 21:
	if ((auto_connect=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Auto_connect must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 22:
	if ((max_clones=val)<0) {
		fprintf(stderr,"NUTS: Invalid value for max_clones on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 23:
	if ((ban_swearing=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Ban_swearing must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 24:
	if (!strcmp(wrd[1],"NONE")) crash_action=0;
	else if (!strcmp(wrd[1],"IGNORE")) crash_action=1;
		else if (!strcmp(wrd[1],"REBOOT")) crash_action=2;
			else {
				fprintf(stderr,"NUTS: Crash_action must be NONE, IGNORE or REBOOT on line %d.\n",config_line);
				boot_exit(1);
				}
	return;

	case 25:
	if ((colour_def=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Colour_def must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 26:
	if ((time_out_afks=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Time_out_afks must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 27:
	if ((allow_caps_in_name=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Allow_caps_in_name must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 28:
	if ((charecho_def=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Charecho_def must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 29:
	if ((time_out_maxlevel=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier for time_out_maxlevel on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;
	}
}



/*** Parse rooms section ***/
parse_rooms_section()
{
static int in_section=0;
int i;
char *ptr1,*ptr2,c;
RM_OBJECT room;

if (!strcmp(wrd[0],"ROOMS:")) {
	if (++in_section>1) {
		fprintf(stderr,"NUTS: Unexpected ROOMS section header on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
if (!wrd[2][0]) {
	fprintf(stderr,"NUTS: Required parameter(s) missing on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[0])>ROOM_NAME_LEN) {
	fprintf(stderr,"NUTS: Room name too long on line %d.\n",config_line);
	boot_exit(1);
	}
/* Check for duplicate name */
for(room=room_first;room!=NULL;room=room->next) {
	if (!strcmp(room->name,wrd[0])) {
		fprintf(stderr,"NUTS: Duplicate room name on line %d.\n",config_line);
		boot_exit(1);
		}
	}

if ((room=create_room())==NULL) {
        fprintf(stderr,"NUTS: unable to create room object.\n");
	boot_exit(1);
        }


strcpy(room->label,wrd[0]);
strcpy(room->name,wrd[0]);

/* Parse internal links bit ie hl,gd,of etc. MUST NOT be any spaces between
   the commas */
i=0;
ptr1=wrd[1];
ptr2=wrd[1];
while(1) {
	while(*ptr2!=',' && *ptr2!='\0') ++ptr2;
	if (*ptr2==',' && *(ptr2+1)=='\0') {
		fprintf(stderr,"NUTS: Missing link label on line %d.\n",config_line);
		boot_exit(1);
		}
	c=*ptr2;  *ptr2='\0';
	if (!strcmp(ptr1,room->label)) {
		fprintf(stderr,"NUTS: Room has a link to itself on line %d.\n",config_line);
		boot_exit(1);
		}
	strcpy(room->link_label[i],ptr1);
	if (c=='\0') break;
	if (++i>=MAX_LINKS) {
		fprintf(stderr,"NUTS: Too many links on line %d.\n",config_line);
		boot_exit(1);
		}
	*ptr2=c;
	ptr1=++ptr2;  
	}


if (!wrd[2][0]) {
        fprintf(stderr,"NUTS: Missing map type in line %d",config_line);
        boot_exit(1);
        }

if (strlen(wrd[2])>MAPTYPE_LEN) {
        fprintf(stderr,"NUTS: Maptype label too long il line %d",config_line);
        boot_exit(1);
        }

strcpy(room->maptype,wrd[2]);


/* Parse access privs */
if (wrd[3][0]=='#') {  room->access=PUBLIC;  return;  }
if (!wrd[3][0] || !strcmp(wrd[3],"BOTH")) room->access=PUBLIC; 
else if (!strcmp(wrd[3],"PUB")) room->access=FIXED_PUBLIC; 
	else if (!strcmp(wrd[3],"PRIV")) room->access=FIXED_PRIVATE;
		else {
			fprintf(stderr,"NUTS: Unknown room access type on line %d.\n",config_line);
			boot_exit(1);
			}
/* Parse external link stuff */
if (!wrd[4][0] || wrd[4][0]=='#') return;
if (!strcmp(wrd[4],"ACCEPT")) {  
	if (wrd[5][0] && wrd[5][0]!='#') {
		fprintf(stderr,"NUTS: Unexpected word following ACCEPT keyword on line %d.\n",config_line);
		boot_exit(1);
		}
	room->inlink=1;  
	return;
	}
if (!strcmp(wrd[4],"CONNECT")) {
	if (!wrd[5][0]) {
		fprintf(stderr,"NUTS: External link name missing on line %d.\n",config_line);
		boot_exit(1);
		}
	if (wrd[6][0] && wrd[6][0]!='#') {
		fprintf(stderr,"NUTS: Unexpected word following external link name on line %d.\n",config_line);
		boot_exit(1);
		}
	strcpy(room->netlink_name,wrd[5]);
	return;
	}
fprintf(stderr,"NUTS: Unknown connection option on line %d.\n",config_line);
boot_exit(1);
}



/*** Parse sites section ***/
parse_sites_section()
{
NL_OBJECT nl;
static int in_section=0;

if (!strcmp(wrd[0],"SITES:")) { 
	if (++in_section>1) {
		fprintf(stderr,"NUTS: Unexpected SITES section header on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
if (!wrd[3][0]) {
	fprintf(stderr,"NUTS: Required parameter(s) missing on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[0])>SERV_NAME_LEN) {
	fprintf(stderr,"NUTS: Link name length too long on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[3])>VERIFY_LEN) {
	fprintf(stderr,"NUTS: Verification too long on line %d.\n",config_line);
	boot_exit(1);
	}
if ((nl=create_netlink())==NULL) {
	fprintf(stderr,"NUTS: Memory allocation failure creating netlink on line %d.\n",config_line);
	boot_exit(1);
	}
if (!wrd[4][0] || wrd[4][0]=='#' || !strcmp(wrd[4],"ALL")) nl->allow=ALL;
else if (!strcmp(wrd[4],"IN")) nl->allow=IN;
	else if (!strcmp(wrd[4],"OUT")) nl->allow=OUT;
		else {
			fprintf(stderr,"NUTS: Unknown netlink access type on line %d.\n",config_line);
			boot_exit(1);
			}
if ((nl->port=atoi(wrd[2]))<1 || nl->port>65535) {
	fprintf(stderr,"NUTS: Illegal port number on line %d.\n",config_line);
	boot_exit(1);
	}
strcpy(nl->service,wrd[0]);
strtolower(wrd[1]);
strcpy(nl->site,wrd[1]);
strcpy(nl->verification,wrd[3]);
}


open_shout_channel()
{
FILE *fp;
CH_OBJECT channel,ch;
char filename[80],line[120],kw[20],*pun;
int kw_code,name_flag=0,line_num=0;
char *remove_first();
char *keyword[]={
"NAME", "LONG", "PHR", "PHRTO","JOIN","UNJOIN","END","*"
};

sprintf(filename,"%s/%s",DATAFILES,CHANNELCONFIG);

if (!(fp=fopen(filename,"r"))) {
	printf("No shout channel configured...\n");
	return;
	}

line[0]='\0';
fgets(line,82,fp);

while (!feof(fp)) {
	line_num++;
	sscanf(line,"%s",kw);
	kw_code=0;
	while(keyword[kw_code][0]!='*') {
		if (!strcmp(keyword[kw_code],kw))  break;
		kw_code++;
		}
	switch (kw_code) {
		case 0:
		if (name_flag) {
			fprintf(stderr,"NUTS: Missing channel END Keyword in line %d\n",line_num);
			boot_exit(1);
			}
		sscanf(line,"%*s %s",text);
		if (strlen(text)>WORD_LEN) {
                        fprintf(stderr,"NUTS: Channel name too long in line %d\n",line_num);
                        boot_exit(1);
                        }
		if (text[0]=='\0') {
			fprintf(stderr,"NUTS: Channel name not specified in line %d\n",line_num);
			boot_exit(1);
			}
		for(ch=ch_first;ch!=NULL;ch=ch->next) {
			if (!(strcmp(ch->name,text))) {
				fprintf(stderr,"NUTS: Duplicate channel name in line %d\n",line_num);
				boot_exit(1);
				}
			}
		if ((channel=create_channel())==NULL) {
			fprintf(stderr,"NUTS: unable to create channel object.\n");
			boot_exit(1);
			}
		strcpy(channel->name,text);
		name_flag=1;
		break;


		case 1:
		if (!name_flag) {
			fprintf(stderr,"NUTS: Long name keyword but not open channel in line %d\n",line_num);
			boot_exit(1);
			}
		pun=remove_first(line);
		line[strlen(line)-1]=0;
		if (strlen(pun)>PHRASE_LEN) {
                        fprintf(stderr,"NUTS: Channel long name too long in line %d\n",line_num);
                        boot_exit(1);
			}
		strcpy(channel->long_name,pun);
		break;


		case 2:
		if (!name_flag) {
			fprintf(stderr,"NUTS: Phrase keyword but not open channel in line %d\n",line_num);
			boot_exit(1);
			}
		pun=remove_first(line);
		line[strlen(line)-1]=0;
		if (strlen(pun)>PHRASE_LEN) {
                        fprintf(stderr,"NUTS: Channel phrase too long in line %d\n",line_num);
                        boot_exit(1);
			}
		strcpy(channel->phrase,pun);
		break;

		case 3:
		if (!name_flag) {
			fprintf(stderr,"NUTS: Phraseto keyword but not open channel in line %d\n",line_num);
			boot_exit(1);
			}
		pun=remove_first(line);
		line[strlen(line)-1]=0;
		if (strlen(pun)>PHRASE_LEN) {
                        fprintf(stderr,"NUTS: Channel phraseto too long in line %d\n",line_num);
                        boot_exit(1);
			}
		strcpy(channel->phraseto,pun);
		break;

		case 4:
		if (!name_flag) {
			fprintf(stderr,"NUTS: join keyword but not open channel in line %d\n",line_num);
			boot_exit(1);
			}
		pun=remove_first(line);
		line[strlen(line)-1]=0;
		if (strlen(pun)>PHRASE_LEN*2) {
                        fprintf(stderr,"NUTS: Channel join too long in line %d\n",line_num);
                        boot_exit(1);
			}
		strcpy(channel->join,pun);
		break;

		case 5:
		if (!name_flag) {
			fprintf(stderr,"NUTS: unjoin keyword but not open channel in line %d\n",line_num);
			boot_exit(1);
			}
		pun=remove_first(line);
		line[strlen(line)-1]=0;
		if (strlen(pun)>PHRASE_LEN*2) {
                        fprintf(stderr,"NUTS: Channel unjoin too long in line %d\n",line_num);
                        boot_exit(1);
			}
		strcpy(channel->unjoin,pun);
		break;

		case 6:
		if (!name_flag) {
			fprintf(stderr,"NUTS: end keyword but not open channel in line %d\n",line_num);
			boot_exit(1);
			}
		if (channel->long_name[0]=='\0') 
			strcpy(channel->long_name,channel->name);
		if (channel->phrase[0]=='\0') 
			strcpy(channel->phrase,"[canale ~OL%3~RS] %1 dice: ");
		if (channel->phraseto[0]=='\0') 
			strcpy(channel->phraseto,"[canale ~OL%3~RS] %1 dice a %2: ");
		if (channel->join[0]=='\0') 
			strcpy(channel->join,"[canale ~OL%3~RS] %1 joina il canale");
		if (channel->unjoin[0]=='\0') 
			strcpy(channel->unjoin,"[canale ~OL%3~RS] %1 lascia il canale");
		name_flag=0;
		printf("channel %s created\n",channel->name);
		break;

		default:		
		fprintf(stderr,"NUTS: Unknown channel Keyword in line %d\n",line_num);
		boot_exit(1);
		}

	line[0]='\0';
	fgets(line,82,fp);
	}

fclose(fp);
}

yn_check(wd)
char *wd;
{
if (!strcmp(wd,"YES")) return 1;
if (!strcmp(wd,"NO")) return 0;
return -1;
}


onoff_check(wd)
char *wd;
{
if (!strcmp(wd,"ON")) return 1;
if (!strcmp(wd,"OFF")) return 0;
return -1;
}


/************ INITIALISATION FUNCTIONS *************/

/*** Initialise globals ***/
init_globals()
{
int i=0;
verification[0]='\0';
port[0]=0;
port[1]=0;
port[2]=0;
auto_connect=1;
max_users=50;
max_clones=1;
ban_swearing=0;
heartbeat=2;
keepalive_interval=60; /* DO NOT TOUCH!!! */
net_idle_time=1800; /* Must be > than the above */
login_idle_time=180;
user_idle_time=300;
time_out_afks=0;
wizport_level=WIZ;
minlogin_level=-1;
mesg_life=1;
no_prompt=0;
num_of_users=0;
num_of_logins=0;
system_logging=1;
password_echo=0;
ignore_sigterm=0;
crash_action=2;
prompt_def=1;
colour_def=1;
charecho_def=0;
time_out_maxlevel=USER;
mesg_check_hour=0;
mesg_check_min=0;
allow_caps_in_name=1;
rs_countdown=0;
rs_announce=0;
rs_which=-1;
rs_user=NULL;
gatecrash_level=GOD+1; /* minimum user level which can enter private rooms */
min_private_users=2; /* minimum num. of users in room before can set to priv */
ignore_mp_level=GOD; /* User level which can ignore the above var. */
rem_user_maxlevel=USER;
rem_user_deflevel=USER;
user_first=NULL;
user_last=NULL;
room_first=NULL;
room_last=NULL; /* This variable isn't used yet */
nl_first=NULL;
nl_last=NULL;
clear_words();
time(&boot_time);
}


/*** Initialise the signal traps etc ***/
init_signals()
{
void sig_handler();

signal(SIGTERM,sig_handler);
signal(SIGSEGV,sig_handler);
signal(SIGBUS,sig_handler);
signal(SIGILL,SIG_IGN);
signal(SIGTRAP,SIG_IGN);
signal(SIGIOT,SIG_IGN);
signal(SIGTSTP,SIG_IGN);
signal(SIGCONT,SIG_IGN);
signal(SIGHUP,SIG_IGN);
signal(SIGINT,SIG_IGN);
signal(SIGQUIT,SIG_IGN);
signal(SIGABRT,SIG_IGN);
signal(SIGFPE,SIG_IGN);
signal(SIGPIPE,SIG_IGN);
signal(SIGTTIN,SIG_IGN);
signal(SIGTTOU,SIG_IGN);
}


/*** Talker signal handler function. Can either shutdown , ignore or reboot
	if a unix error occurs though if we ignore it we're living on borrowed
	time as usually it will crash completely after a while anyway. ***/
void sig_handler(sig)
int sig;
{
force_listen=1;
switch(sig) {
	case SIGTERM:
	if (ignore_sigterm) {
		write_syslog("SIGTERM signal received - ignoring.\n",1,TOSYS);
		return;
		}
	write_room(NULL,"\n\n~OLSYSTEM:~FR~LI SIGTERM received, initiating shutdown!\n\n");
	talker_shutdown(NULL,"a termination signal (SIGTERM)",0); 

	case SIGSEGV:
	switch(crash_action) {
		case 0:	
		write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI PANIC - Segmentation fault, initiating shutdown!\n\n");
		talker_shutdown(NULL,"a segmentation fault (SIGSEGV)",0); 

		case 1:	
		write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI WARNING - A segmentation fault has just occured!\n\n");
		write_syslog("WARNING: A segmentation fault occured!\n",1,TOSYS);
		longjmp(jmpvar,0);

		case 2:
		write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI PANIC - Segmentation fault, initiating reboot!\n\n");
		talker_shutdown(NULL,"a segmentation fault (SIGSEGV)",1); 
		}

	case SIGBUS:
	switch(crash_action) {
		case 0:
		write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI PANIC - Bus error, initiating shutdown!\n\n");
		talker_shutdown(NULL,"a bus error (SIGBUS)",0);

		case 1:
		write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI WARNING - A bus error has just occured!\n\n");
		write_syslog("WARNING: A bus error occured!\n",1,TOSYS);
		longjmp(jmpvar,0);

		case 2:
		write_room(NULL,"\n\n\07~OLSYSTEM:~FR~LI PANIC - Bus error, initiating reboot!\n\n");
		talker_shutdown(NULL,"a bus error (SIGBUS)",1);
		}
	}
}

	
/*** Initialise sockets on ports ***/
init_sockets()
{
struct sockaddr_in bind_addr;
int i,on,size;

printf("Initialising sockets on ports: %d, %d, %d\n",port[0],port[1],port[2]);
on=1;
size=sizeof(struct sockaddr_in);
bind_addr.sin_family=AF_INET;
bind_addr.sin_addr.s_addr=INADDR_ANY;
for(i=0;i<3;++i) {
	/* create sockets */
	if ((listen_sock[i]=socket(AF_INET,SOCK_STREAM,0))==-1) boot_exit(i+2);

	/* allow reboots on port even with TIME_WAITS */
	setsockopt(listen_sock[i],SOL_SOCKET,SO_REUSEADDR,(char *)&on,sizeof(on));

	/* bind sockets and set up listen queues */
	bind_addr.sin_port=htons(port[i]);
	if (bind(listen_sock[i],(struct sockaddr *)&bind_addr,size)==-1) 
		boot_exit(i+5);
	if (listen(listen_sock[i],10)==-1) boot_exit(i+8);

	/* Set to non-blocking , do we need this? Not really. */
	fcntl(listen_sock[i],F_SETFL,O_NDELAY);
	}
}


/*** Initialise connections to remote servers. Basically this tries to connect
     to the services listed in the config file and it puts the open sockets in 
	the NL_OBJECT linked list which the talker then uses ***/
init_connections()
{
NL_OBJECT nl;
RM_OBJECT rm;
int ret,cnt=0;

printf("Connecting to remote servers...\n");
errno=0;
for(rm=room_first;rm!=NULL;rm=rm->next) {
	if ((nl=rm->netlink)==NULL) continue;
	++cnt;
	printf("  Trying service %s at %s %d: ",nl->service,nl->site,nl->port);
	fflush(stdout);
	if ((ret=connect_to_site(nl))) {
		if (ret==1) {
			printf("%s.\n",sys_errlist[errno]);
			sprintf(text,"NETLINK: Failed to connect to %s: %s.\n",nl->service,sys_errlist[errno]);
			}
		else {
			printf("Unknown hostname.\n");
			sprintf(text,"NETLINK: Failed to connect to %s: Unknown hostname.\n",nl->service);
			}
		write_syslog(text,1,TOSYS);
		}
	else {
		printf("CONNECTED.\n");
		sprintf(text,"NETLINK: Connected to %s (%s %d).\n",nl->service,nl->site,nl->port);
		write_syslog(text,1,TOSYS);
		nl->connect_room=rm;
		}
	}
if (cnt) printf("  See system log for any further information.\n");
else printf("  No remote connections configured.\n");
}


/*** Do the actual connection ***/
connect_to_site(nl)
NL_OBJECT nl;
{
struct sockaddr_in con_addr;
struct hostent *he;
int inetnum;
char *sn;

sn=nl->site;
/* See if number address */
while(*sn && (*sn=='.' || isdigit(*sn))) sn++;

/* Name address given */
if(*sn) {
	if(!(he=gethostbyname(nl->site))) return 2;
	memcpy((char *)&con_addr.sin_addr,he->h_addr,(size_t)he->h_length);
	}
/* Number address given */
else {
	if((inetnum=inet_addr(nl->site))==-1) return 1;
	memcpy((char *)&con_addr.sin_addr,(char *)&inetnum,(size_t)sizeof(inetnum));
	}
/* Set up stuff and disable interrupts */
if ((nl->socket=socket(AF_INET,SOCK_STREAM,0))==-1) return 1;
con_addr.sin_family=AF_INET;
con_addr.sin_port=htons(nl->port);
signal(SIGALRM,SIG_IGN);

/* Attempt the connect. This is where the talker may hang. */
if (connect(nl->socket,(struct sockaddr *)&con_addr,sizeof(con_addr))==-1) {
	reset_alarm();  return 1;
	}
reset_alarm();
nl->type=OUTGOING;
nl->stage=VERIFYING;
nl->last_recvd=time(0);
return 0;
}

	

/************* WRITE FUNCTIONS ************/

/*** Write a NULL terminated string to a socket ***/
write_sock(sock,str)
int sock;
char *str;
{
write(sock,str,strlen(str));
}



/*** Send message to user ***/
write_user(user,str)
UR_OBJECT user;
char *str;
{
int buffpos,sock,i;
char *start,buff[OUT_BUFF_SIZE],mesg[ARR_SIZE],*colour_com_strip();

if (user==NULL) return;
if (user->type==REMOTE_TYPE) {
	if (user->netlink->ver_major<=3 
	    && user->netlink->ver_minor<2) str=colour_com_strip(str);
	if (str[strlen(str)-1]!='\n') 
		sprintf(mesg,"MSG %s\n%s\nEMSG\n",user->name,str);
	else sprintf(mesg,"MSG %s\n%sEMSG\n",user->name,str);
	write_sock(user->netlink->socket,mesg);
	return;
	}
start=str;
buffpos=0;
sock=user->socket;
/* Process string and write to buffer. We use pointers here instead of arrays 
   since these are supposedly much faster (though in reality I guess it depends
   on the compiler) which is necessary since this routine is used all the 
   time. */
while(*str) {
	if (*str=='\n') {
		if (buffpos>OUT_BUFF_SIZE-6) {
			write(sock,buff,buffpos);  buffpos=0;
			}
		/* Reset terminal before every newline */
		if (user->colour) {
			memcpy(buff+buffpos,"\033[0m",4);  buffpos+=4;
			}
		*(buff+buffpos)='\n';  *(buff+buffpos+1)='\r';  
		buffpos+=2;  ++str;
		}
	else {  
		/* See if its a / before a ~ , if so then we print colour command
		   as text */
		if (*str=='/' && *(str+1)=='~') {  ++str;  continue;  }
		if (str!=start && *str=='~' && *(str-1)=='/') {
			*(buff+buffpos)=*str;  goto CONT;
			}
		/* Process colour commands eg ~FR. We have to strip out the commands 
		   from the string even if user doesnt have colour switched on hence 
		   the user->colour check isnt done just yet */
		if (*str=='~') {
			if (buffpos>OUT_BUFF_SIZE-6) {
				write(sock,buff,buffpos);  buffpos=0;
				}
			++str;
			for(i=0;i<NUM_COLS;++i) {
				if (!strncmp(str,colcom[i],2)) {
					if (user->colour) {
						memcpy(buff+buffpos,colcode[i],strlen(colcode[i]));
						buffpos+=strlen(colcode[i])-1;  
						}
					else buffpos--;
					++str;
					goto CONT;
					}
				}
			*(buff+buffpos)=*(--str);
			}
		else *(buff+buffpos)=*str;
		CONT:
		++buffpos;   ++str; 
		}
	if (buffpos==OUT_BUFF_SIZE) {
		write(sock,buff,OUT_BUFF_SIZE);  buffpos=0;
		}
	}
if (buffpos) write(sock,buff,buffpos);
/* Reset terminal at end of string */
if (user->colour) write_sock(sock,"\033[0m"); 
}



/*** Write to users of level 'level' and above or below depending on above
     variable; if 1 then above else below ***/
write_level(level,above,str,user)
int level,above;
char *str;
UR_OBJECT user;
{
UR_OBJECT u;

for(u=user_first;u!=NULL;u=u->next) {
	if (u!=user && !u->login && u->type!=CLONE_TYPE) {
		if ((above && u->level>=level) || (!above && u->level<=level)) 
			write_user(u,str);
		}
	}
}



/*** Subsid function to below but this one is used the most ***/
write_room(rm,str)
RM_OBJECT rm;
char *str;
{
write_room_except(rm,str,NULL);
}



/*** Write to everyone in room rm except for "user". If rm is NULL write 
     to all rooms. ***/
write_room_except(rm,str,user)
RM_OBJECT rm;
char *str;
UR_OBJECT user;
{
UR_OBJECT u;
char text2[ARR_SIZE];
char tvar[ROOM_NAME_LEN+1];

for(u=user_first;u!=NULL;u=u->next) {
	if (u->login 
	    || u->room==NULL 
	    || (u->room!=rm && rm!=NULL) 
	    || (u->ignall && !force_listen)
	    || u==user) continue;
	if (u->type==CLONE_TYPE) {
		if (u->clone_hear==CLONE_HEAR_NOTHING || u->owner->ignall) continue;
		/* Ignore anything not in clones room, eg shouts, system messages
		   and semotes since the clones owner will hear them anyway. */
		if (rm!=u->room) continue;
		if (u->clone_hear==CLONE_HEAR_SWEARS) {
			if (!contains_swearing(str)) continue;
			}
		strcpy(tvar,u->room->name);
		delump(tvar);
		sprintf(text2,"~FT[ %s ]:~RS %s",tvar,str);
		write_user(u->owner,text2);
		}
	else write_user(u,str); 
	}
}

write_room_except2(rm,str,user,user_b)
RM_OBJECT rm;
char *str;
UR_OBJECT user, user_b;
{
UR_OBJECT u;
char text2[ARR_SIZE];
char tvar[ROOM_NAME_LEN+1];

for(u=user_first;u!=NULL;u=u->next) {
        if (u->login
            || u->room==NULL
            || (u->room!=rm && rm!=NULL)
            || (u->ignall && !force_listen)
            || u==user || u==user_b) continue;
        if (u->type==CLONE_TYPE) {
                if (u->clone_hear==CLONE_HEAR_NOTHING || u->owner->ignall) continue;

                if (rm!=u->room) continue;
                if (u->clone_hear==CLONE_HEAR_SWEARS) {
                        if (!contains_swearing(str)) continue;
                        }
                strcpy(tvar,u->room->name);
		delump(tvar);
		sprintf(text2,"~FT[ %s ]:~RS %s",tvar,str);
                write_user(u->owner,text2);
                }
        else write_user(u,str);
        }
}

write_to_listener_users(str,channel)
char *str;
CH_OBJECT channel;
{
int i;
UR_OBJECT u;

for(u=user_first;u!=NULL;u=u->next) {
	if (u->login 
	    || u->room==NULL 
	    || (u->ignall)) continue;
	for (i=0;i<MAX_USER_CHANNEL;++i) if (u->channel[i]==channel) write_user(u,str);
	}
}










/*** Write a string to system log ***/
write_syslog(str,write_time,fil)
char *str;
int write_time;
{
FILE *fp;
char filename[80];

switch (fil) {
        case TOSYS : sprintf(filename,"%s/%s.log",LOGDIR,SYSLOG); break;
        case TOACCOUNT: sprintf(filename,"%s/%s.log",LOGDIR,ACCOUNTFILE);break;
        case TOROOM   : sprintf(filename,"%s/%s.log",LOGDIR,ROOMFILE); break;
	case TONICK   : sprintf(filename,"%s/%s.log",LOGDIR,NICKFILE); break;
        default: sprintf(filename,"%s/%s.log",LOGDIR,SYSLOG);
        }



if (!system_logging || !(fp=fopen(filename,"a"))) return;
if (!write_time) fputs(str,fp);
else fprintf(fp,"%02d/%02d %02d:%02d:%02d: %s",tmday,tmonth+1,thour,tmin,tsec,str);
fclose(fp);
}



/******** LOGIN/LOGOUT FUNCTIONS ********/

/*** Login function. Lots of nice inline code :) ***/
login(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
int i;
char name[ARR_SIZE],passwd[ARR_SIZE];

name[0]='\0';  passwd[0]='\0';
switch(user->login) {
	case 4:
	sscanf(inpstr,"%s",name);
	if(name[0]<33) {
		write_user(user,"\nGive me a name: ");  return;
		}
	if (!strcmp(name,"quit")) {
		write_user(user,"\n\n*** Abandoning login attempt ***\n\n");
		disconnect_user(user);  return;
		}
	if (!strcmp(name,"who")) {
		sprintf(text,"Who request from site %s\n",user->site);
		write_syslog(text,1,TOSYS);
		who(user,0);  
		write_user(user,"\nGive me a name: ");
		return;
		}
	if (!strcmp(name,"version")) {
		sprintf(text,"\nNUTS version %s\n\nGive me a name: ",VERSION);
		write_user(user,text);  return;
		}
	if (strlen(name)<3) {
		write_user(user,"\nName too short.\n\n");  
		attempts(user);  return;
		}
	if (strlen(name)>USER_NAME_LEN) {
		write_user(user,"\nName too long.\n\n");
		attempts(user);  return;
		}
	/* see if only letters in login */
	for (i=0;i<strlen(name);++i) {
		if (!isalpha(name[i])) {
			write_user(user,"\nOnly letters are allowed in a name.\n\n");
			attempts(user);  return;
			}
		}
	if (!allow_caps_in_name) strtolower(name);
	name[0]=toupper(name[0]);
	if (user_banned(name)) {
		write_user(user,"\nYou are banned from this talker.\n\n");
		disconnect_user(user);
		sprintf(text,"Attempted login by banned user %s.\n",name);
		write_syslog(text,1,TOSYS);
		return;
		}
	strcpy(user->name,name);
	/* If user has hung on another login clear that session */
	for(u=user_first;u!=NULL;u=u->next) {
		if (u->login && u!=user && !strcmp(u->name,user->name)) {
			disconnect_user(u);  break;
			}
		}	
	if (!load_user_details(user)) {
		if (user->port==port[1]) {
			write_user(user,"\nSorry, new logins cannot be created on this port.\n\n");
			disconnect_user(user);  
			return;
			}
		if (minlogin_level>-1) {
			write_user(user,"\nSorry, new logins cannot be created at this time.\n\n");
			disconnect_user(user);  
			return;
			}
		write_user(user,"New user...\n");
		}
	else {
		if (user->port==port[1] && user->level<wizport_level) {
			sprintf(text,"\nSorry, only users of level %s and above can log in on this port.\n\n",level_name[0][wizport_level]);
			write_user(user,text);
			disconnect_user(user);  
			return;
			}
		if (user->level<minlogin_level) {
			write_user(user,"\nSorry, the talker is locked out to users of your level.\n\n");
			disconnect_user(user);  
			return;
			}
		if (user->level>SYSOP) {
			write_user(user,"\nSorry, the talker is locked out to users of your level.\n\n");
			sprintf(text,"WARNING! : User %s has a level of %d from %s!\n",user->name,user->level,user->site);
			write_syslog(text,1,TOSYS);
			disconnect_user(user);
			return;
			}
		}
	write_user(user,"Give me a password: ");
	echo_off(user);
	user->login=3;
	return;

	case 3:
	sscanf(inpstr,"%s",passwd);
	if (strlen(passwd)<3) {
		write_user(user,"\n\nPassword too short.\n\n");  
		attempts(user);  return;
		}
	if (strlen(passwd)>PASS_LEN) {
		write_user(user,"\n\nPassword too long.\n\n");
		attempts(user);  return;
		}
	/* if new user... */
	if (!user->pass[0]) {
		strcpy(user->pass,(char *)crypt(passwd,"NU"));
		write_user(user,"\nPlease confirm password: ");
		user->login=2;
		}
	else {
		if (!strcmp(user->pass,(char *)crypt(passwd,"NU"))) {
			echo_on(user);  connect_user(user);  return;
			}
		sprintf(text,"Error login to user %s from %s\n",user->name,user->site);
		write_syslog(text,1,TOSYS);
		write_user(user,"\n\nIncorrect login.\n\n");
		attempts(user);
		}
	return;

	case 2:
	sscanf(inpstr,"%s",passwd);
	if (strcmp(user->pass,(char*)crypt(passwd,"NU"))) {
		write_user(user,"\n\nPasswords do not match.\n\n");
		attempts(user);
		return;
		}
	write_user(user,"\n\nSex (M/F) :");
	echo_on(user);
	user->login=1;
	return;

	case 1:
	inpstr[0]=toupper(inpstr[0]);
	if (inpstr[0]=='M' || inpstr[0]=='F') user->sex=inpstr[0];
	else {   
		write_user(user,"\n\nIncorrect value\n\n");
                attempts(user);
                return;
                }
	strcpy(user->desc,"hasn't used .desc yet");
	strcpy(user->in_phrase,"enters");	
	strcpy(user->out_phrase,"goes");	
	user->last_site[0]='\0';
	user->level=0;
	user->muzzled=0;
	user->accreq=0;
	user->command_mode=0;
	user->prompt=prompt_def;
	user->colour=colour_def;
	user->charmode_echo=charecho_def;
	user->path=0;
	user->read_mail=time(0);
	user->last_login=time(0);
	user->total_login=0;
	user->last_login_len=0;
	save_user_details(user,1);
	sprintf(text,"New user \"%s\" created.\n",user->name);
	write_syslog(text,1,TOSYS);
	sprintf(text,"%s\n",user->name);
	write_syslog(text,0,TONICK);
	connect_user(user);
	}
}
	


/*** Count up attempts made by user to login ***/
attempts(user)
UR_OBJECT user;
{
int i;

user->attempts++;
if (user->attempts==3) {
	write_user(user,"\nMaximum attempts reached.\n\n");
	disconnect_user(user);  return;
	}
user->login=4;
user->pass[0]='\0';
for (i=0;i<MAX_USER_CHANNEL;++i) user->channel[i]=NULL;
user->room=NULL;
write_user(user,"Give me a name: ");
echo_on(user);
}



/*** Load the users details ***/
load_user_details(user)
UR_OBJECT user;
{
FILE *fp;
RM_OBJECT get_room();
char *remove_first();
char line[120],filename[80];
int temp1,temp2,temp3,kw_code,i;
char kw[20],*pun,temp[120];
CH_OBJECT ch;
char *keyword[]={
"NAME","PASS","DATA","SITE","DESC","INPHR","OUTPHR","CHAN","ROOM","*"
};

sprintf(filename,"%s/%s.D",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) return 0;

fgets(line,82,fp);

while (!feof(fp)) {
	sscanf(line,"%s",kw);
        kw_code=0;
        while(keyword[kw_code][0]!='*') {
                if (!strcmp(keyword[kw_code],kw))  break;
                kw_code++;
               	}
	switch(kw_code) {
		case 0: break;
		case 1: /* password */
			sscanf(line,"%*s %s",user->pass);
			break;
		case 2: /* data */
			sscanf(line,"%*s %d %d %d %d %d %d %d %d %d %d %d %c",&temp1,&temp2,&user->last_login_len,&temp3,&user->level,&user->prompt,&user->muzzled,&user->charmode_echo,&user->command_mode,&user->colour,&user->path,&user->sex);
			user->last_login=(time_t)temp1;
			user->total_login=(time_t)temp2;
			user->read_mail=(time_t)temp3;
			break;
		case 3: /* last site */
			sscanf(line,"%*s %s\n",user->last_site);
			break;
		case 4: /* desc */
			pun=remove_first(line);
			line[strlen(line)-1]=0;
			strcpy(user->desc,pun);
			break;
		case 5: /* inphr */
			pun=remove_first(line);
			line[strlen(line)-1]=0;
			strcpy(user->in_phrase,pun); 
			break;
		case 6: /* outphr */
			pun=remove_first(line);
			line[strlen(line)-1]=0;
			strcpy(user->out_phrase,pun);
			break;
		case 7: /* channel */
			sscanf(line,"%*s %s",temp);
			for (ch=ch_first;ch!=NULL;ch=ch->next) {
				if (!(strcmp(ch->name,temp))) {
					for(i=0;i<MAX_USER_CHANNEL;++i)
						if (user->channel[i]==NULL) break;
					if (i==MAX_USER_CHANNEL) break;
					user->channel[i]=ch;
					}
				}
			break;
		
		case 8: /* room */
			sscanf(line,"%*s %s",temp);
			if ((user->room=get_room(temp,NULL))==NULL)
				user->room=room_first;
			else if (user->room->access==PRIVATE)
				user->room=room_first;
			break;

		}
	fgets(line,82,fp);
	}

fclose(fp);
return 1;
}



/*** Save a users stats ***/
save_user_details(user,save_current)
UR_OBJECT user;
int save_current;
{
FILE *fp;
char filename[80];
int i;

if (user->type==REMOTE_TYPE || user->type==CLONE_TYPE) return 0;
sprintf(filename,"%s/%s.D",USERFILES,user->name);
if (!(fp=fopen(filename,"w"))) {
	sprintf(text,"%s: failed to save your details.\n",syserror);	
	write_user(user,text);
	sprintf(text,"SAVE_USER_STATS: Failed to save %s's details.\n",user->name);
	write_syslog(text,1,TOSYS);
	return 0;
	}
fprintf(fp,"NAME %s\n",user->name);
fprintf(fp,"PASS %s\n",user->pass);
if (save_current)
	fprintf(fp,"DATA %d %d %d ",(int)time(0),(int)user->total_login,(int)(time(0)-user->last_login));
else fprintf(fp,"DATA %d %d %d ",(int)user->last_login,(int)user->total_login,user->last_login_len);
fprintf(fp,"%d %d %d %d %d %d %d %d %c\n",(int)user->read_mail,user->level,user->prompt,user->muzzled,user->charmode_echo,user->command_mode,user->colour,user->path,user->sex);
if (save_current) fprintf(fp,"SITE %s\n",user->site);
else fprintf(fp,"SITE %s\n",user->last_site);
fprintf(fp,"DESC %s\n",user->desc);
fprintf(fp,"INPHR %s\n",user->in_phrase);
fprintf(fp,"OUTPHR %s\n",user->out_phrase);
for(i=0;i<MAX_USER_CHANNEL;++i) {
	if (user->channel[i]==NULL) break;
	fprintf(fp,"CHAN %s\n",user->channel[i]->name);
	}
fprintf(fp,"ROOM %s\n",user->room->name);
fclose(fp);
return 1;
}


/*** Connect the user to the talker proper ***/
connect_user(user)
UR_OBJECT user;
{
UR_OBJECT u,u2;
RM_OBJECT rm;
char temp[30];

/* See if user already connected */
for(u=user_first;u!=NULL;u=u->next) {
	if (user!=u && user->type!=CLONE_TYPE && !strcmp(user->name,u->name)) {
		rm=u->room;
		if (u->type==REMOTE_TYPE) {
			write_user(u,"\n~FB~OLYou are pulled back through cyberspace...\n");
			sprintf(text,"REMVD %s\n",u->name);
			write_sock(u->netlink->socket,text);
			sprintf(text,"%s vanishes.\n",u->name);
			destruct_user(u);
			write_room(rm,text);
			reset_access(rm);
			num_of_users--;
			break;
			}
		write_user(user,"\n\nYou are already connected - switching to old session...\n");
		sprintf(text,"%s swapped sessions.\n",user->name);
		write_syslog(text,1,TOSYS);
		close(u->socket);
		u->socket=user->socket;
		strcpy(u->site,user->site);
		u->site_port=user->site_port;
		destruct_user(user);
		num_of_logins--;
		sprintf(text,"~OLSESSION SWAP:~RS %s %s\n",u->name,u->desc);
		write_room_except(rm,text,u);
		if (rm==NULL) {
			sprintf(text,"ACT %s look\n",u->name);
			write_sock(u->netlink->socket,text);
			}
		else {
			look(u,0);  prompt(u);
			}
		/* Reset the sockets on any clones */
		for(u2=user_first;u2!=NULL;u2=u2->next) {
			if (u2->type==CLONE_TYPE && u2->owner==user) {
				u2->socket=u->socket;  u->owner=u;
				}
			}
		return;
		}
	}
/* Announce users logon. You're probably wondering why Ive done it this strange
   way , well its to avoid a minor bug that arose when as in 3.3.1 it created 
   the string in 2 parts and sent the parts seperately over a netlink. If you 
   want more details email me. */	
sprintf(text,"~OLSIGN ON:~RS %s %s\n",user->name,user->desc);
write_level(HELPER,0,text,NULL);
sprintf(text,"~OLSIGN ON:~RS %s %s  ~RS~FT(%s:%d)\n",user->name,user->desc,user->site,user->site_port);
write_level(MAGHETTO,1,text,NULL);

/* send post-login message and other logon stuff to user */
write_user(user,"\n");
more(user,user->socket,MOTD2); 
if (user->last_site[0]) {
	sprintf(temp,"%s",ctime(&user->last_login));
	temp[strlen(temp)-1]=0;
	sprintf(text,"Welcome %s...\n\nYou were last logged in on %s from %s.\n\n",user->name,temp,user->last_site);
	}
else sprintf(text,"Welcome %s...\n\n",user->name);
write_user(user,text);
if (user->room==NULL) user->room=room_first;
user->last_login=time(0); /* set to now */
sprintf(text,"~FTYour level is:~RS~OL %s\n",level_name[user->path][user->level]);
write_user(user,text);
look(user,0);
if (has_unread_mail(user)) write_user(user,"\07~FT~OL~LI** YOU HAVE UNREAD MAIL **\n");
prompt(user);

/* write to syslog and set up some vars */
sprintf(text,"%s logged in on port %d from %s:%d.\n",user->name,user->port,user->site,user->site_port);
write_syslog(text,1,TOSYS);
num_of_users++;
num_of_logins--;
user->login=0;
}


/*** Disconnect user from talker ***/
disconnect_user(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;

rm=user->room;
if (user->login) {
	close(user->socket);  
	destruct_user(user);
	num_of_logins--;  
	return;
	}
if (user->type!=REMOTE_TYPE) {
	save_user_details(user,1);  
	sprintf(text,"%s logged out.\n",user->name);
	write_syslog(text,1,TOSYS);
	write_user(user,"\n~OL~FYYou are removed from this reality...\n\n");
	close(user->socket);
	sprintf(text,"~OLSIGN OFF:~RS %s %s\n",user->name,user->desc);
	write_room(NULL,text);
	if (user->room==NULL) {
		sprintf(text,"REL %s\n",user->name);
		write_sock(user->netlink->socket,text);
		for(nl=nl_first;nl!=NULL;nl=nl->next) 
			if (nl->mesg_user==user) {  
				nl->mesg_user=(UR_OBJECT)-1;  break;  
				}
		}
	}
else {
	write_user(user,"\n~FR~OLYou are pulled back in disgrace to your own domain...\n");
	sprintf(text,"REMVD %s\n",user->name);
	write_sock(user->netlink->socket,text);
	sprintf(text,"~FR~OL%s is banished from here!\n",user->name);
	write_room_except(rm,text,user);
	sprintf(text,"NETLINK: Remote user %s removed.\n",user->name);
	write_syslog(text,1,TOSYS);
	}
if (user->malloc_start!=NULL) free(user->malloc_start);
num_of_users--;

/* Destroy any clones */
destroy_user_clones(user);
destruct_user(user);
reset_access(rm);
destructed=0;
}


/*** Tell telnet not to echo characters - for password entry ***/
echo_off(user)
UR_OBJECT user;
{
char seq[4];

if (password_echo) return;
sprintf(seq,"%c%c%c",255,251,1);
write_user(user,seq);
}


/*** Tell telnet to echo characters ***/
echo_on(user)
UR_OBJECT user;
{
char seq[4];

if (password_echo) return;
sprintf(seq,"%c%c%c",255,252,1);
write_user(user,seq);
}



/************ MISCELLANIOUS FUNCTIONS *************/

/*** Stuff that is neither speech nor a command is dealt with here ***/
misc_ops(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
switch(user->misc_op) {
	case 1: 
	if (toupper(inpstr[0])=='Y') {
		if (rs_countdown && !rs_which) {
			if (rs_countdown>60) 
				sprintf(text,"\n\07~OLSYSTEM: ~FR~LISHUTDOWN INITIATED, shutdown in %d minutes, %d seconds!\n\n",rs_countdown/60,rs_countdown%60);
			else sprintf(text,"\n\07~OLSYSTEM: ~FR~LISHUTDOWN INITIATED, shutdown in %d seconds!\n\n",rs_countdown);
			write_room(NULL,text);
			sprintf(text,"%s initiated a %d seconds SHUTDOWN countdown.\n",user->name,rs_countdown);
			write_syslog(text,1,TOSYS);
			rs_user=user;
			rs_announce=time(0);
			user->misc_op=0;  
			prompt(user);
			return 1;
			}
		talker_shutdown(user,NULL,0); 
		}
	/* This will reset any reboot countdown that was started, oh well */
	rs_countdown=0;
	rs_announce=0;
	rs_which=-1;
	rs_user=NULL;
	user->misc_op=0;  
	prompt(user);
	return 1;

	case 2: 
	if (toupper(inpstr[0])=='E'
	    || more(user,user->socket,user->page_file)!=1) {
		user->misc_op=0;  user->filepos=0;  user->page_file[0]='\0';
		prompt(user); 
		}
	return 1;

	case 3: /* writing on board */
	case 4: /* Writing mail */
	case 5: /* doing profile */
	editor(user,inpstr,MAX_LINES);  return 1;

	case 6:
	if (toupper(inpstr[0])=='Y') delete_user(user,1); 
	else {  user->misc_op=0;  prompt(user);  }
	return 1;

	case 7:
	if (toupper(inpstr[0])=='Y') {
		if (rs_countdown && rs_which==1) {
			if (rs_countdown>60) 
				sprintf(text,"\n\07~OLSYSTEM: ~FY~LIREBOOT INITIATED, rebooting in %d minutes, %d seconds!\n\n",rs_countdown/60,rs_countdown%60);
			else sprintf(text,"\n\07~OLSYSTEM: ~FY~LIREBOOT INITIATED, rebooting in %d seconds!\n\n",rs_countdown);
			write_room(NULL,text);
			sprintf(text,"%s initiated a %d seconds REBOOT countdown.\n",user->name,rs_countdown);
			write_syslog(text,1,TOSYS);
			rs_user=user;
			rs_announce=time(0);
			user->misc_op=0;  
			prompt(user);
			return 1;
			}
		talker_shutdown(user,NULL,1); 
		}
	if (rs_which==1 && rs_countdown && rs_user==NULL) {
		rs_countdown=0;
		rs_announce=0;
		rs_which=-1;
		}
	user->misc_op=0;  
	prompt(user);
	return 1;

	case 8: /* room_desc */
	editor(user,inpstr,ROOM_LINES); return 1;

	case 9: /* aspect */
	editor(user,inpstr,ASPECT_LINES); return 1;

	case 10: /* macro */
	case 11:
	return 1;


	}
return 0;
}


/*** The editor used for writing profiles, mail and messages on the boards ***/
editor(user,inpstr,max_lines)
UR_OBJECT user;
char *inpstr;
int max_lines;
{
int cnt,line;
char *edprompt="\n~FGSave~RS, ~FYredo~RS or ~FRabort~RS (s/r/a): ";
char *ptr;

if (user->edit_op) {
	switch(toupper(*inpstr)) {
		case 'S':
		sprintf(text,"%s finishes composing some text.\n",user->name);
		write_room_except(user->room,text,user);
		switch(user->misc_op) {
			case 3: write_board(user,NULL,1);  break;
			case 4: smail(user,NULL,1);  break;
			case 5: enter_profile(user,1);  break;
			case 8: room_desc(user,1); break;
			case 9: aspect(user,1); break;	
			}
		editor_done(user);
		return;

		case 'R':
		user->edit_op=0;
		user->edit_line=1;
		user->charcnt=0;
		user->malloc_end=user->malloc_start;
		*user->malloc_start='\0';
		sprintf(text,"\nRedo message...\n\n%d>",user->edit_line);
		write_user(user,text);
		return;

		case 'A':
		write_user(user,"\nMessage aborted.\n");
		sprintf(text,"%s gives up composing some text.\n",user->name);
		write_room_except(user->room,text,user);
		editor_done(user);  
		return;

		default: 
		write_user(user,edprompt);
		return;
		}
	}
/* Allocate memory if user has just started editing */
if (user->malloc_start==NULL) {
	if ((user->malloc_start=(char *)malloc(max_lines*81))==NULL) {
		sprintf(text,"%s: failed to allocate buffer memory.\n",syserror);
		write_user(user,text);
		write_syslog("ERROR: Failed to allocate memory in editor().\n",0,TOSYS);
		user->misc_op=0;
		prompt(user);
		return;
		}
	user->ignall_store=user->ignall;
	user->ignall=1; /* Dont want chat mucking up the edit screen */
	user->edit_line=1;
	user->charcnt=0;
	user->malloc_end=user->malloc_start;
	*user->malloc_start='\0';
	sprintf(text,"~FTMaximum of %d lines, end with a '.' on a line by itself.\n\n1>",max_lines);
	write_user(user,text);
	sprintf(text,"%s starts composing some text...\n",user->name);
	write_room_except(user->room,text,user);
	return;
	}

/* Check for empty line */
if (!word_count) {
	if (!user->charcnt) {
		sprintf(text,"%d>",user->edit_line);
		write_user(user,text);
		return;
		}
	*user->malloc_end++='\n';
	if (user->edit_line==max_lines) goto END;
	sprintf(text,"%d>",++user->edit_line);
     write_user(user,text);
	user->charcnt=0;
     return;
	}
/* If nothing carried over and a dot is entered then end */
if (!user->charcnt && !strcmp(inpstr,".")) goto END;

line=user->edit_line;
cnt=user->charcnt;

/* loop through input and store in allocated memory */
while(*inpstr) {
	*user->malloc_end++=*inpstr++;
	if (++cnt==80) {  user->edit_line++;  cnt=0;  }
	if (user->edit_line>max_lines 
	    || user->malloc_end - user->malloc_start>=max_lines*81) goto END;
	}
if (line!=user->edit_line) {
	ptr=(char *)(user->malloc_end-cnt);
	*user->malloc_end='\0';
	sprintf(text,"%d>%s",user->edit_line,ptr);
	write_user(user,text);
	user->charcnt=cnt;
	return;
	}
else {
	*user->malloc_end++='\n';
	user->charcnt=0;
	}
if (user->edit_line!=max_lines) {
	sprintf(text,"%d>",++user->edit_line);
	write_user(user,text);
	return;
	}

/* User has finished his message , prompt for what to do now */
END:
*user->malloc_end='\0';
if (*user->malloc_start) {
	write_user(user,edprompt);
	user->edit_op=1;  return;
	}
write_user(user,"\nNo text.\n");
sprintf(text,"%s gives up composing some text.\n",user->name);
write_room_except(user->room,text,user);
editor_done(user);
}


/*** Reset some values at the end of editing ***/
editor_done(user)
UR_OBJECT user;
{
user->misc_op=0;
user->edit_op=0;
user->edit_line=0;
free(user->malloc_start);
user->malloc_start=NULL;
user->malloc_end=NULL;
user->ignall=user->ignall_store;
prompt(user);
}


/*** Record speech and emotes in the room. ***/
record(rm,str)
RM_OBJECT rm;
char *str;
{
strncpy(rm->revbuff[rm->revline],str,REVIEW_LEN);
rm->revbuff[rm->revline][REVIEW_LEN]='\n';
rm->revbuff[rm->revline][REVIEW_LEN+1]='\0';
rm->revline=(rm->revline+1)%REVIEW_LINES;
}


/*** Records tells and pemotes sent to the user. ***/
record_tell(user,str)
UR_OBJECT user;
char *str;
{
strncpy(user->revbuff[user->revline],str,REVIEW_LEN);
user->revbuff[user->revline][REVIEW_LEN]='\n';
user->revbuff[user->revline][REVIEW_LEN+1]='\0';
user->revline=(user->revline+1)%REVTELL_LINES;
}

/*** Records shouts and semotes ***/
record_shout(str,channel) 
char *str;
CH_OBJECT channel;
{
strncpy(channel->revshout[channel->revline],str,REVIEW_LEN);
channel->revshout[channel->revline][REVIEW_LEN]='\n';
channel->revshout[channel->revline][REVIEW_LEN+1]='\0';
channel->revline=(channel->revline+1)%REVSHOUT_LINES;
}


/*** Set room access back to public if not enough users in room ***/
reset_access(rm)
RM_OBJECT rm;
{
UR_OBJECT u;
int cnt;

if (rm==NULL || rm->access!=PRIVATE) return; 
cnt=0;
for(u=user_first;u!=NULL;u=u->next) if (u->room==rm) ++cnt;
if (cnt<min_private_users) {
	write_room(rm,"Room access returned to ~FGPUBLIC.\n");
	rm->access=PUBLIC;

	/* Reset any invites into the room & clear review buffer */
	for(u=user_first;u!=NULL;u=u->next) {
		if (u->invite_room==rm) u->invite_room=NULL;
		}
	clear_revbuff(rm);
	}
}



/*** Exit because of error during bootup ***/
boot_exit(code)
int code;
{
switch(code) {
	case 1:
	write_syslog("BOOT FAILURE: Error while parsing configuration file.\n",0,TOSYS);
	exit(1);

	case 2:
	perror("NUTS: Can't open main port listen socket");
	write_syslog("BOOT FAILURE: Can't open main port listen socket.\n",0,TOSYS);
	exit(2);

	case 3:
	perror("NUTS: Can't open wiz port listen socket");
	write_syslog("BOOT FAILURE: Can't open wiz port listen socket.\n",0,TOSYS);
	exit(3);

	case 4:
	perror("NUTS: Can't open link port listen socket");
	write_syslog("BOOT FAILURE: Can't open link port listen socket.\n",0,TOSYS);
	exit(4);

	case 5:
	perror("NUTS: Can't bind to main port");
	write_syslog("BOOT FAILURE: Can't bind to main port.\n",0,TOSYS);
	exit(5);

	case 6:
	perror("NUTS: Can't bind to wiz port");
	write_syslog("BOOT FAILURE: Can't bind to wiz port.\n",0,TOSYS);
	exit(6);

	case 7:
	perror("NUTS: Can't bind to link port");
	write_syslog("BOOT FAILURE: Can't bind to link port.\n",0,TOSYS);
	exit(7);
	
	case 8:
	perror("NUTS: Listen error on main port");
	write_syslog("BOOT FAILURE: Listen error on main port.\n",0,TOSYS);
	exit(8);

	case 9:
	perror("NUTS: Listen error on wiz port");
	write_syslog("BOOT FAILURE: Listen error on wiz port.\n",0,TOSYS);
	exit(9);

	case 10:
	perror("NUTS: Listen error on link port");
	write_syslog("BOOT FAILURE: Listen error on link port.\n",0,TOSYS);
	exit(10);

	case 11:
	perror("NUTS: Failed to fork");
	write_syslog("BOOT FAILURE: Failed to fork.\n",0,TOSYS);
	exit(11);
	}
}



/*** User prompt ***/
prompt(user)
UR_OBJECT user;
{
int hr,min;
char *str,*colour_com_strip();

if (no_prompt) return;
if (user->type==REMOTE_TYPE) {
	sprintf(text,"PRM %s\n",user->name);
	write_sock(user->netlink->socket,text);  
	return;
	}
if (user->command_mode && !user->misc_op) {  
	if (!user->vis) write_user(user,"~FTCOM+> ");
	else write_user(user,"~FTCOM> ");  
	return;  
	}
if (!user->prompt || user->misc_op) return;
hr=(int)(time(0)-user->last_login)/3600;
min=((int)(time(0)-user->last_login)%3600)/60;
str=colour_com_strip(user->desc);

if (!user->vis)
	sprintf(text,"~FT<%02d:%02d, %02d:%02d, %s %s +>\n",thour,tmin,hr,min,user->name,str);
else sprintf(text,"~FT<%02d:%02d, %02d:%02d, %s %s>\n",thour,tmin,hr,min,user->name,str);
write_user(user,text);
}



/*** Page a file out to user. Colour commands in files will only work if 
     user!=NULL since if NULL we dont know if his terminal can support colour 
     or not. Return values: 
	        0 = cannot find file, 1 = found file, 2 = found and finished ***/
more(user,sock,filename)
UR_OBJECT user;
int sock;
char *filename;
{
int i,buffpos,num_chars,lines,retval,len;
char buff[OUT_BUFF_SIZE],text2[83],*str,*colour_com_strip();
FILE *fp;

if (!(fp=fopen(filename,"r"))) {
	if (user!=NULL) user->filepos=0;  
	return 0;
	}
/* jump to reading posn in file */
if (user!=NULL) fseek(fp,user->filepos,0);

text[0]='\0';  
buffpos=0;  
num_chars=0;
retval=1; 
len=0;

/* If user is remote then only do 1 line at a time */
if (sock==-1) {
	lines=1;  fgets(text2,82,fp);
	}
else {
	lines=0;  fgets(text,sizeof(text)-1,fp);
	}

/* Go through file */
while(!feof(fp) && (lines<23 || user==NULL)) {
	if (sock==-1) {
		lines++;  
		if (user->netlink->ver_major<=3 && user->netlink->ver_minor<2) 
			str=colour_com_strip(text2);
		else str=text2;
		if (str[strlen(str)-1]!='\n') 
			sprintf(text,"MSG %s\n%s\nEMSG\n",user->name,str);
		else sprintf(text,"MSG %s\n%sEMSG\n",user->name,str);
		write_sock(user->netlink->socket,text);
		num_chars+=strlen(str);
		fgets(text2,82,fp);
		continue;
		}
	str=text;

	/* Process line from file */
	while(*str) {
		if (*str=='\n') {
			if (buffpos>OUT_BUFF_SIZE-6) {
				write(sock,buff,buffpos);  buffpos=0;
				}
			/* Reset terminal before every newline */
			if (user!=NULL && user->colour) {
				memcpy(buff+buffpos,"\033[0m",4);  buffpos+=4;
				}
			*(buff+buffpos)='\n';  *(buff+buffpos+1)='\r';  
			buffpos+=2;  ++str;
			}
		else {  
			/* Process colour commands in the file. See write_user()
			   function for full comments on this code. */
			if (*str=='/' && *(str+1)=='~') {  ++str;  continue;  }
			if (str!=text && *str=='~' && *(str-1)=='/') {
				*(buff+buffpos)=*str;  goto CONT;
				}
			if (*str=='~') {
				if (buffpos>OUT_BUFF_SIZE-6) {
					write(sock,buff,buffpos);  buffpos=0;
					}
				++str;
				for(i=0;i<NUM_COLS;++i) {
					if (!strncmp(str,colcom[i],2)) {
						if (user!=NULL && user->colour) {
							memcpy(buffpos+buff,colcode[i],strlen(colcode[i]));
							buffpos+=strlen(colcode[i])-1;
							}
						else buffpos--;
						++str;
						goto CONT;
						}
					}
				*(buff+buffpos)=*(--str);
				}
			else *(buff+buffpos)=*str;
			CONT:
			++buffpos;   ++str;
			}
		if (buffpos==OUT_BUFF_SIZE) {
			write(sock,buff,OUT_BUFF_SIZE);  buffpos=0;
			}
		}
	len=strlen(text);
	num_chars+=len;
	lines+=len/80+(len<80);
	fgets(text,sizeof(text)-1,fp);
	}
if (buffpos && sock!=-1) write(sock,buff,buffpos);

/* if user is logging on dont page file */
if (user==NULL) {  fclose(fp);  return 2;  };
if (feof(fp)) {
	user->filepos=0;  no_prompt=0;  retval=2;
	}
else  {
	/* store file position and file name */
	user->filepos+=num_chars;
	strcpy(user->page_file,filename);
	/* We use E here instead of Q because when on a remote system and
	   in COMMAND mode the Q will be intercepted by the home system and 
	   quit the user */
	write_user(user,"           ~BB*** Press <return> to continue, 'e'<return> to exit ***~RS");
	no_prompt=1;
	}
fclose(fp);
return retval;
}



/*** Set global vars. hours,minutes,seconds,date,day,month,year ***/
set_date_time()
{
struct tm *tm_struct; /* structure is defined in time.h */
time_t tm_num;

/* Set up the structure */
time(&tm_num);
tm_struct=localtime(&tm_num);

/* Get the values */
tday=tm_struct->tm_yday;
tyear=1900+tm_struct->tm_year; /* Will this work past the year 2000? Hmm... */
tmonth=tm_struct->tm_mon;
tmday=tm_struct->tm_mday;
twday=tm_struct->tm_wday;
thour=tm_struct->tm_hour;
tmin=tm_struct->tm_min;
tsec=tm_struct->tm_sec; 
}



/*** Return pos. of second word in inpstr ***/
char *remove_first(inpstr)
char *inpstr;
{
char *pos=inpstr;
while(*pos<33 && *pos) ++pos;
while(*pos>32) ++pos;
while(*pos<33 && *pos) ++pos;
return pos;
}


delump(inp)
char *inp;
{
while (*inp!='\0') {
	if (*inp=='_') {
		*inp=' ';
		}
	inp++;
	}
}

inump(inp)
char *inp;
{
while (*inp!='\0') {
	if (*inp==' ') {
		*inp='_';
		}
	inp++;
	}
}

get_end(inp)
char *inp;
{
char *pun;
pun=inp;


       while (*pun!='\0') {
                if ((*pun==' ' && *(pun+1)==' ') || (*pun==' ' && *(pun+1)=='\0')) {
                        *pun='\0';
                        break;
                        }
                pun++;
                }
}


remove_from_user_list(name)
char *name;
{
FILE *fp,*temp;
char filename[80];
char str[USER_NAME_LEN+1];

sprintf(filename,"%s/%s.log",LOGDIR,NICKFILE);

if (!(fp=fopen(filename,"r"))) {
	write_syslog("ERROR: couldn't open file NICKFILE in remove_from_user_list()\n",1,TOSYS);
	return;
	}

if (!(temp=fopen("tempfile","w"))) {
	write_syslog("ERROR: couldn't open tempfile in remove_from_user_list()\n",1,TOSYS);
	fclose(fp);
        return;
        }

fscanf(fp,"%s",str);

while(!feof(fp)) {
	if (strcmp(str,name)) fprintf(temp,"%s\n",str);
	fscanf(fp,"%s",str);
	}

fclose(fp);
fclose(temp);

rename("tempfile",filename);

}



/*** Get user struct pointer from name ***/
UR_OBJECT get_user(name)
char *name;
{
UR_OBJECT u;

name[0]=toupper(name[0]);

/* Search for exact name */
for(u=user_first;u!=NULL;u=u->next) {
	if (u->login || u->type==CLONE_TYPE) continue;
	if (!strcmp(u->name,name))  return u;
	}

/* Search for close match name */
for(u=user_first;u!=NULL;u=u->next) {
	if (u->login || u->type==CLONE_TYPE) continue;
	if (strstr(u->name,name))  return u;
	}
return NULL;
}


/*** Get room struct pointer from abbreviated name ***/
RM_OBJECT get_room(name,user)
char *name;
UR_OBJECT user;
{
RM_OBJECT rm,room;
char tvar1[ROOM_NAME_LEN+1],tvar2[ROOM_NAME_LEN+1];
int cnt=0,cnt2=0;
text[0]='\0';

strcpy(tvar2,name);
strtolower(tvar2);

for (rm=room_first;rm!=NULL;rm=rm->next) {
        strcpy(tvar1,rm->name);
        strtolower(tvar1);
        if (!strcmp(tvar1,tvar2)) return rm;
}

if (user==NULL) return NULL;

for (cnt=0;cnt<MAX_LINKS;++cnt) {
	if (user->room->link[cnt]==NULL) continue;
        strcpy(tvar1,user->room->link[cnt]->name);
        strtolower(tvar1);
        if (!strncmp(tvar1,tvar2,strlen(tvar2))) { 
		rm=user->room->link[cnt];
		return rm;
		}
}

cnt=0;

for(rm=room_first;rm!=NULL;rm=rm->next) {
        strcpy(tvar1,rm->name);
        strtolower(tvar1);
        if (!strncmp(tvar1,tvar2,strlen(tvar2))) { 
		if (cnt2>sizeof(text)-ROOM_NAME_LEN*2 || cnt>=20) {
			write_user(user,"The shortcut is too generic\n");
			return NULL;
			}
		strcat(text,tvar1);
		strcat(text,"\n");
		cnt++;
		cnt2+=(strlen(tvar1)+1);
		room=rm;
		}
	}

if (!cnt) {
	write_user(user,nosuchroom);
	return NULL;
	}

if (cnt>1) {
	write_user(user,"This shortcut can be referred to:\n");
	delump(text);
	write_user(user,text);
	sprintf(text,"Total of %d rooms\nThe shortcut is too generic\n",cnt);
	write_user(user,text);
	return NULL;
	}

return room;
}


/*** Return level value based on level name ***/
get_level(name)
char *name;
{
int i,j;
char level[30];

for (j=0; j<=MAX_LEVEL_TYPE; j++) {
	i=0;
	while(level_name[j][i][0]!='*') {
		strcpy(level,level_name[j][i]);
		strtoupper(level);
		if (!strcmp(level,name)) return i;
		++i;
		}
	}
return -1;
}

get_emotion(str)
char *str;
{
int i=0;

while (emochar_array[i][0]!='*') {
	if (!(strcmp(str,emochar_array[i]))) return i;
	i++;
	}
return -1;
}
	

/*** See if a user has access to a room. If room is fixed to private then
	it is considered a wizroom so grant permission to any user of WIZ and
	above for those. ***/
has_room_access(user,rm)
UR_OBJECT user;
RM_OBJECT rm;
{
if ((rm->access & PRIVATE) 
    && user->level<=gatecrash_level 
    && user->invite_room!=rm
    && !((rm->access & FIXED) && user->level>=WIZ)) return 0;
return 1;
}


/*** See if user has unread mail, mail file has last read time on its 
     first line ***/
has_unread_mail(user)
UR_OBJECT user;
{
FILE *fp;
int tm;
char filename[80];

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%d",&tm);
fclose(fp);
if (tm>(int)user->read_mail) return 1;
return 0;
}


/*** This is function that sends mail to other users ***/
send_mail(user,to,ptr)
UR_OBJECT user;
char *to,*ptr;
{
NL_OBJECT nl;
FILE *infp,*outfp;
char *c,d,*service,filename[80],line[DNL+1];

/* See if remote mail */
c=to;  service=NULL;
while(*c) {
	if (*c=='@') {  
		service=c+1;  *c='\0'; 
		for(nl=nl_first;nl!=NULL;nl=nl->next) {
			if (!strcmp(nl->service,service) && nl->stage==UP) {
				send_external_mail(nl,user,to,ptr);
				return;
				}
			}
		sprintf(text,"Service %s unavailable.\n",service);
		write_user(user,text); 
		return;
		}
	++c;
	}

/* Local mail */
if (!(outfp=fopen("tempfile","w"))) {
	write_user(user,"Error in mail delivery.\n");
	write_syslog("ERROR: Couldn't open tempfile in send_mail().\n",0,TOSYS);
	return;
	}
/* Write current time on first line of tempfile */
fprintf(outfp,"%d\r",(int)time(0));

/* Copy current mail file into tempfile if it exists */
sprintf(filename,"%s/%s.M",USERFILES,to);
if (infp=fopen(filename,"r")) {
	/* Discard first line of mail file. */
	fgets(line,DNL,infp);

	/* Copy rest of file */
	d=getc(infp);  
	while(!feof(infp)) {  putc(d,outfp);  d=getc(infp);  }
	fclose(infp);
	}

/* Put new mail in tempfile */
if (user!=NULL) {
	if (user->type==REMOTE_TYPE)
		fprintf(outfp,"~OLFrom: %s@%s  %s\n",user->name,user->netlink->service,long_date(0));
	else fprintf(outfp,"~OLFrom: %s  %s\n",user->name,long_date(0));
	}
else fprintf(outfp,"~OLFrom: MAILER  %s\n",long_date(0));

fputs(ptr,outfp);
fputs("\n",outfp);
fclose(outfp);
rename("tempfile",filename);
write_user(user,"Mail sent.\n");
write_user(get_user(to),"\07~FT~OL~LI** YOU HAVE NEW MAIL **\n");
}


/*** Spool mail file and ask for confirmation of users existence on remote
	site ***/
send_external_mail(nl,user,to,ptr)
NL_OBJECT nl;
UR_OBJECT user;
char *to,*ptr;
{
FILE *fp;
char filename[80];

/* Write out to spool file first */
sprintf(filename,"%s/OUT_%s_%s@%s",MAILSPOOL,user->name,to,nl->service);
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: unable to spool mail.\n",syserror);
	write_user(user,text);
	sprintf(text,"ERROR: Couldn't open file %s to append in send_external_mail().\n",filename);
	write_syslog(text,0,TOSYS);
	return;
	}
putc('\n',fp);
fputs(ptr,fp);
fclose(fp);

/* Ask for verification of users existence */
sprintf(text,"EXISTS? %s %s\n",to,user->name);
write_sock(nl->socket,text);

/* Rest of delivery process now up to netlink functions */
write_user(user,"Mail sent.\n");
}


/*** See if string contains any swearing ***/
contains_swearing(str)
char *str;
{
char *s;
int i;

if ((s=(char *)malloc(strlen(str)+1))==NULL) {
	write_syslog("ERROR: Failed to allocate memory in contains_swearing().\n",0,TOSYS);
	return 0;
	}
strcpy(s,str);
strtolower(s); 
i=0;
while(swear_words[i][0]!='*') {
	if (strstr(s,swear_words[i])) {  free(s);  return 1;  }
	++i;
	}
free(s);
return 0;
}


/*** Count the number of colour commands in a string ***/
colour_com_count(str)
char *str;
{
char *s;
int i,cnt;

s=str;  cnt=0;
while(*s) {
     if (*s=='~') {
          ++s;
          for(i=0;i<NUM_COLS;++i) {
               if (!strncmp(s,colcom[i],2)) {
                    cnt++;  s++;  continue;
                    }
               }
          continue;
          }
     ++s;
     }
return cnt;
}


/*** Strip out colour commands from string for when we are sending strings
     over a netlink to a talker that doesn't support them ***/
char *colour_com_strip(str)
char *str;
{
char *s,*t;
static char text2[ARR_SIZE];
int i;

s=str;  t=text2;
while(*s) {
	if (*s=='~') {
		++s;
		for(i=0;i<NUM_COLS;++i) {
			if (!strncmp(s,colcom[i],2)) {  s++;  goto CONT;  }
			}
		--s;  *t++=*s;
		}
	else *t++=*s;
	CONT:
	s++;
	}	
*t='\0';
return text2;
}


/*** Date string for board messages, mail, .who and .allclones ***/
char *long_date(which)
int which;
{
static char dstr[80];

if (which) sprintf(dstr,"on %s %d %s %d at %02d:%02d",day[twday],tmday,month[tmonth],tyear,thour,tmin);
else sprintf(dstr,"[ %s %d %s %d at %02d:%02d ]",day[twday],tmday,month[tmonth],tyear,thour,tmin);
return dstr;
}


/*** Clear the review buffer in the room ***/
clear_revbuff(rm)
RM_OBJECT rm;
{
int c;
for(c=0;c<REVIEW_LINES;++c) rm->revbuff[c][0]='\0';
rm->revline=0;
}


/*** Clear the screen ***/
cls(user)
UR_OBJECT user;
{
int i;

for(i=0;i<5;++i) write_user(user,"\n\n\n\n\n\n\n\n\n\n");		
}


/*** Convert string to upper case ***/
strtoupper(str)
char *str;
{
while(*str) {  *str=toupper(*str);  str++; }
}


/*** Convert string to lower case ***/
strtolower(str)
char *str;
{
while(*str) {  *str=tolower(*str);  str++; }
}


/*** Returns 1 if string is a positive number ***/
isnumber(str)
char *str;
{
while(*str) if (!isdigit(*str++)) return 0;
return 1;
}


/************ OBJECT FUNCTIONS ************/

/*** Construct user/clone object ***/
UR_OBJECT create_user()
{
UR_OBJECT user;
int i;

if ((user=(UR_OBJECT)malloc(sizeof(struct user_struct)))==NULL) {
	write_syslog("ERROR: Memory allocation failure in create_user().\n",1,TOSYS);
	return NULL;
	}

/* Append object into linked list. */
if (user_first==NULL) {  
	user_first=user;  user->prev=NULL;  
	}
else {  
	user_last->next=user;  user->prev=user_last;  
	}
user->next=NULL;
user_last=user;

/* initialise user structure */
user->type=USER_TYPE;
user->name[0]='\0';
user->desc[0]='\0';
user->sex='U';
user->path=0;
user->in_phrase[0]='\0'; 
user->out_phrase[0]='\0';   
user->afk_mesg[0]='\0';
user->pass[0]='\0';
user->site[0]='\0';
user->site_port=0;
user->last_site[0]='\0';
user->page_file[0]='\0';
user->mail_to[0]='\0';
user->inpstr_old[0]='\0';
user->buff[0]='\0';  
user->buffpos=0;
user->filepos=0;
user->read_mail=time(0);
user->room=NULL;
user->invite_room=NULL;
user->port=0;
user->login=0;
user->socket=-1;
user->attempts=0;
user->command_mode=0;
user->level=0;
user->vis=1;
user->ignall=0;
user->ignall_store=0;
user->igntell=0;
user->muzzled=0;
user->remote_com=-1;
user->netlink=NULL;
user->pot_netlink=NULL; 
user->last_input=time(0);
user->last_login=time(0);
user->last_login_len=0;
user->total_login=0;
user->prompt=prompt_def;
user->colour=colour_def;
user->charmode_echo=charecho_def;
user->misc_op=0;
user->edit_op=0;
user->edit_line=0;
user->charcnt=0;
user->warned=0;
user->accreq=1;
user->afk=0;
user->revline=0;
user->clone_hear=CLONE_HEAR_ALL;
user->malloc_start=NULL;
user->malloc_end=NULL;
user->owner=NULL;
for(i=0;i<REVTELL_LINES;++i) user->revbuff[i][0]='\0';
for(i=0;i<MAX_USER_CHANNEL;++i) user->channel[i]=NULL;
return user;
}



/*** Destruct an object. ***/
destruct_user(user)
UR_OBJECT user;
{
/* Remove from linked list */
if (user==user_first) {
	user_first=user->next;
	if (user==user_last) user_last=NULL;
	else user_first->prev=NULL;
	}
else {
	user->prev->next=user->next;
	if (user==user_last) { 
		user_last=user->prev;  user_last->next=NULL; 
		}
	else user->next->prev=user->prev;
	}
free(user);
destructed=1;
}


destruct_room(room)
RM_OBJECT room;
{
RM_OBJECT prev,count;

if (room==room_first) {
        room_first=room->next;
        free(room);
        return;
        }

for (count=room_first;count!=NULL;count=count->next)
        if (count->next==room)  prev=count;

if (room==room_last) {
        room_last=prev;
        room_last->next= NULL;
        free(room);
        return;
        }

prev->next=room->next;
free(room);
}
 

/*** Construct room object ***/
RM_OBJECT create_room()
{
RM_OBJECT room;
int i;

if ((room=(RM_OBJECT)malloc(sizeof(struct room_struct)))==NULL) {
	write_syslog("ERROR: Memory allocation failure in create_room().\n",1,TOSYS);
	return NULL;
	}
room->name[0]='\0';
room->label[0]='\0';
room->topic[0]='\0';
room->maptype[0]='\0';
room->access=-1;
room->revline=0;
room->mesg_cnt=0;
room->inlink=0;
room->netlink=NULL;
room->netlink_name[0]='\0';
room->next=NULL;
for(i=0;i<MAX_LINKS;++i) {
	room->link_label[i][0]='\0';  room->link[i]=NULL;
	}
for(i=0;i<REVIEW_LINES;++i) room->revbuff[i][0]='\0';
if (room_first==NULL) room_first=room;
else room_last->next=room;
room_last=room;
return room;
}


/*** Construct link object ***/
NL_OBJECT create_netlink()
{
NL_OBJECT nl;

if ((nl=(NL_OBJECT)malloc(sizeof(struct netlink_struct)))==NULL) {
	sprintf(text,"NETLINK: Memory allocation failure in create_netlink().\n");
	write_syslog(text,1,TOSYS);
	return NULL;
	}
if (nl_first==NULL) { 
	nl_first=nl;  nl->prev=NULL;  nl->next=NULL;
	}
else {  
	nl_last->next=nl;  nl->next=NULL;  nl->prev=nl_last;
	}
nl_last=nl;

nl->service[0]='\0';
nl->site[0]='\0';
nl->verification[0]='\0';
nl->mail_to[0]='\0';
nl->mail_from[0]='\0';
nl->mailfile=NULL;
nl->buffer[0]='\0';
nl->ver_major=0;
nl->ver_minor=0;
nl->ver_patch=0;
nl->keepalive_cnt=0;
nl->last_recvd=0;
nl->port=0;
nl->socket=0;
nl->mesg_user=NULL;
nl->connect_room=NULL;
nl->type=UNCONNECTED;
nl->stage=DOWN;
nl->connected=0;
nl->lastcom=-1;
nl->allow=ALL;
nl->warned=0;
return nl;
}


/*** Destruct a netlink (usually a closed incoming one). ***/
destruct_netlink(nl)
NL_OBJECT nl;
{
if (nl!=nl_first) {
	nl->prev->next=nl->next;
	if (nl!=nl_last) nl->next->prev=nl->prev;
	else { nl_last=nl->prev; nl_last->next=NULL; }
	}
else {
	nl_first=nl->next;
	if (nl!=nl_last) nl_first->prev=NULL;
	else nl_last=NULL; 
	}
free(nl);
}


/*** Destroy all clones belonging to given user ***/
destroy_user_clones(user)
UR_OBJECT user;
{
UR_OBJECT u;

for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->owner==user) {
		sprintf(text,"The clone of %s shimmers and vanishes.\n",u->name);
		write_room(u->room,text);
		destruct_user(u);
		}
	}
}





/*** Construct room object ***/
CH_OBJECT create_channel()
{
CH_OBJECT channel;
int i;

if ((channel=(CH_OBJECT)malloc(sizeof(struct channel_struct)))==NULL) {
	write_syslog("ERROR: Memory allocation failure in create_channel().\n",1,TOSYS);
	return NULL;
	}
channel->name[0]='\0';
channel->long_name[0]='\0';
channel->phrase[0]='\0';
channel->phraseto[0]='\0';
channel->join[0]='\0';
channel->unjoin[0]='\0';
for(i=0;i<REVSHOUT_LINES;++i) channel->revshout[i][0]='\0';
channel->revline=0;
channel->next=NULL;

if (ch_first==NULL) ch_first=channel;
else ch_last->next=channel;
ch_last=channel;
return channel;
}



destruct_channel(channel)
CH_OBJECT channel;
{
CH_OBJECT prev,count;

if (channel==ch_first) {
        ch_first=channel->next;
        free(channel);
        return;
        }

for (count=ch_first;count!=NULL;count=count->next)
        if (count->next==channel)  prev=count;

if (channel==ch_last) {
        ch_last=prev;
        ch_last->next=NULL;
        free(channel);
        return;
        }

prev->next=channel->next;
free(channel);
}





/************ NUTS PROTOCOL AND LINK MANAGEMENT FUNCTIONS ************/
/* Please don't alter these functions. If you do you may introduce 
   incompatabilities which may prevent other systems connecting or cause
   bugs on the remote site and yours. You may think it looks simple but
   even the newline count is important in some places. */

/*** Accept incoming server connection ***/
accept_server_connection(sock,acc_addr)
int sock;
struct sockaddr_in acc_addr;
{
NL_OBJECT nl,nl2,create_netlink();
RM_OBJECT rm;
char site[81];

/* Send server type id and version number */
sprintf(text,"NUTS %s\n",VERSION);
write_sock(sock,text);
strcpy(site,get_ip_address(acc_addr));
sprintf(text,"NETLINK: Received request connection from site %s.\n",site);
write_syslog(text,1,TOSYS);

/* See if legal site, ie site is in config sites list. */
for(nl2=nl_first;nl2!=NULL;nl2=nl2->next) 
	if (!strcmp(nl2->site,site)) goto OK;
write_sock(sock,"DENIED CONNECT 1\n");
close(sock);
write_syslog("NETLINK: Request denied, remote site not in valid sites list.\n",1,TOSYS);
return;

/* Find free room link */
OK:
for(rm=room_first;rm!=NULL;rm=rm->next) {
	if (rm->netlink==NULL && rm->inlink) {
		if ((nl=create_netlink())==NULL) {
			write_sock(sock,"DENIED CONNECT 2\n");  
			close(sock);  
			write_syslog("NETLINK: Request denied, unable to create netlink object.\n",1,TOSYS);
			return;
			}
		rm->netlink=nl;
		nl->socket=sock;
		nl->type=INCOMING;
		nl->stage=VERIFYING;
		nl->connect_room=rm;
		nl->allow=nl2->allow;
		nl->last_recvd=time(0);
		strcpy(nl->service,"<verifying>");
		strcpy(nl->site,site);
		write_sock(sock,"GRANTED CONNECT\n");
		write_syslog("NETLINK: Request granted.\n",1,TOSYS);
		return;
		}
	}
write_sock(sock,"DENIED CONNECT 3\n");
close(sock);
write_syslog("NETLINK: Request denied, no free room links.\n",1,TOSYS);
}
		

/*** Deal with netlink data on link nl ***/
exec_netcom(nl,inpstr)
NL_OBJECT nl;
char *inpstr;
{
int netcom_num,lev;
char w1[ARR_SIZE],w2[ARR_SIZE],w3[ARR_SIZE],*c,ctemp;

/* The most used commands have been truncated to save bandwidth, ie ACT is
   short for action, EMSG is short for end message. Commands that don't get
   used much ie VERIFICATION have been left long for readability. */
char *netcom[]={
"DISCONNECT","TRANS","REL","ACT","GRANTED",
"DENIED","MSG","EMSG","PRM","VERIFICATION",
"VERIFY","REMVD","ERROR","EXISTS?","EXISTS_NO",
"EXISTS_YES","MAIL","ENDMAIL","MAILERROR","KA",
"RSTAT","*"
};

/* The buffer is large (ARR_SIZE*2) but if a bug occurs with a remote system
   and no newlines are sent for some reason it may overflow and this will 
   probably cause a crash. Oh well, such is life. */
if (nl->buffer[0]) {
	strcat(nl->buffer,inpstr);  inpstr=nl->buffer;
	}
nl->last_recvd=time(0);

/* Go through data */
while(*inpstr) {
	w1[0]='\0';  w2[0]='\0';  w3[0]='\0';  lev=0;
	if (*inpstr!='\n') sscanf(inpstr,"%s %s %s %d",w1,w2,w3,&lev);
	/* Find first newline */
	c=inpstr;  ctemp=1; /* hopefully we'll never get char 1 in the string */
	while(*c) {
		if (*c=='\n') {  ctemp=*(c+1); *(c+1)='\0';  break; }
		++c;
		}
	/* If no newline then input is incomplete, store and return */
	if (ctemp==1) {  
		if (inpstr!=nl->buffer) strcpy(nl->buffer,inpstr);  
		return;  
		}
	/* Get command number */
	netcom_num=0;
	while(netcom[netcom_num][0]!='*') {
		if (!strcmp(netcom[netcom_num],w1))  break;
		netcom_num++;
		}
	/* Deal with initial connects */
	if (nl->stage==VERIFYING) {
		if (nl->type==OUTGOING) {
			if (strcmp(w1,"NUTS")) {
				sprintf(text,"NETLINK: Incorrect connect message from %s.\n",nl->service);
				write_syslog(text,1,TOSYS);
				shutdown_netlink(nl);
				return;
				}	
			/* Store remote version for compat checks */
			nl->stage=UP;
			w2[10]='\0'; 
			sscanf(w2,"%d.%d.%d",&nl->ver_major,&nl->ver_minor,&nl->ver_patch);
			goto NEXT_LINE;
			}
		else {
			/* Incoming */
			if (netcom_num!=9) {
				/* No verification, no connection */
				sprintf(text,"NETLINK: No verification sent by site %s.\n",nl->site);
				write_syslog(text,1,TOSYS);
				shutdown_netlink(nl);  
				return;
				}
			nl->stage=UP;
			}
		}
	/* If remote is currently sending a message relay it to user, don't
	   interpret it unless its EMSG or ERROR */
	if (nl->mesg_user!=NULL && netcom_num!=7 && netcom_num!=12) {
		/* If -1 then user logged off before end of mesg received */
		if (nl->mesg_user!=(UR_OBJECT)-1) write_user(nl->mesg_user,inpstr);   
		goto NEXT_LINE;
		}
	/* Same goes for mail except its ENDMAIL or ERROR */
	if (nl->mailfile!=NULL && netcom_num!=17) {
		fputs(inpstr,nl->mailfile);  goto NEXT_LINE;
		}
	nl->lastcom=netcom_num;
	switch(netcom_num) {
		case  0: 
		if (nl->stage==UP) {
			sprintf(text,"~OLSYSTEM:~FY~RS Disconnecting from service %s in the %s.\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			}
		shutdown_netlink(nl);  
		break;

		case  1: nl_transfer(nl,w2,w3,lev,inpstr);  break;
		case  2: nl_release(nl,w2);  break;
		case  3: nl_action(nl,w2,inpstr);  break;
		case  4: nl_granted(nl,w2);  break;
		case  5: nl_denied(nl,w2,inpstr);  break;
		case  6: nl_mesg(nl,w2); break;
		case  7: nl->mesg_user=NULL;  break;
		case  8: nl_prompt(nl,w2);  break;
		case  9: nl_verification(nl,w2,w3,0);  break;
		case 10: nl_verification(nl,w2,w3,1);  break;
		case 11: nl_removed(nl,w2);  break;
		case 12: nl_error(nl);  break;
		case 13: nl_checkexist(nl,w2,w3);  break;
		case 14: nl_user_notexist(nl,w2,w3);  break;
		case 15: nl_user_exist(nl,w2,w3);  break;
		case 16: nl_mail(nl,w2,w3);  break;
		case 17: nl_endmail(nl);  break;
		case 18: nl_mailerror(nl,w2,w3);  break;
		case 19: /* Keepalive signal, do nothing */ break;
		case 20: nl_rstat(nl,w2);  break;
		default: 
			sprintf(text,"NETLINK: Received unknown command '%s' from %s.\n",w1,nl->service);
			write_syslog(text,1,TOSYS);
			write_sock(nl->socket,"ERROR\n"); 
		}
	NEXT_LINE:
	/* See if link has closed */
	if (nl->type==UNCONNECTED) return;
	*(c+1)=ctemp;
	inpstr=c+1;
	}
nl->buffer[0]='\0';
}


/*** Deal with user being transfered over from remote site ***/
nl_transfer(nl,name,pass,lev,inpstr)
NL_OBJECT nl;
char *name,*pass,*inpstr;
int lev;
{
UR_OBJECT u,create_user();

/* link for outgoing users only */
if (nl->allow==OUT) {
	sprintf(text,"DENIED %s 4\n",name);
	write_sock(nl->socket,text);
	return;
	}
if (strlen(name)>USER_NAME_LEN) name[USER_NAME_LEN]='\0';

/* See if user is banned */
if (user_banned(name)) {
	if (nl->ver_major==3 && nl->ver_minor>=3 && nl->ver_patch>=3) 
		sprintf(text,"DENIED %s 9\n",name); /* new error for 3.3.3 */
	else sprintf(text,"DENIED %s 6\n",name); /* old error to old versions */
	write_sock(nl->socket,text);
	return;
	}

/* See if user already on here */
if (u=get_user(name)) {
	sprintf(text,"DENIED %s 5\n",name);
	write_sock(nl->socket,text);
	return;
	}

/* See if user of this name exists on this system by trying to load up
   datafile */
if ((u=create_user())==NULL) {		
	sprintf(text,"DENIED %s 6\n",name);
	write_sock(nl->socket,text);
	return;
	}
u->type=REMOTE_TYPE;
strcpy(u->name,name);
if (load_user_details(u)) {
	if (strcmp(u->pass,pass)) {
		/* Incorrect password sent */
		sprintf(text,"DENIED %s 7\n",name);
		write_sock(nl->socket,text);
		destruct_user(u);
		destructed=0;
		return;
		}
	}
else {
	/* Get the users description */
	if (nl->ver_major<=3 && nl->ver_minor<=3 && nl->ver_patch<1) 
		strcpy(text,remove_first(remove_first(remove_first(inpstr))));
	else strcpy(text,remove_first(remove_first(remove_first(remove_first(inpstr)))));
	text[USER_DESC_LEN]='\0';
	terminate(text);
	strcpy(u->desc,text);
	strcpy(u->in_phrase,"enters");
	strcpy(u->out_phrase,"goes");
	if (nl->ver_major==3 && nl->ver_minor>=3 && nl->ver_patch>=1) {
		if (lev>rem_user_maxlevel) u->level=rem_user_maxlevel;
		else u->level=lev; 
		}
	else u->level=rem_user_deflevel;
	}
/* See if users level is below minlogin level */
if (u->level<minlogin_level) {
	if (nl->ver_major==3 && nl->ver_minor>=3 && nl->ver_patch>=3) 
		sprintf(text,"DENIED %s 8\n",u->name); /* new error for 3.3.3 */
	else sprintf(text,"DENIED %s 6\n",u->name); /* old error to old versions */
	write_sock(nl->socket,text);
	destruct_user(u);
	destructed=0;
	return;
	}
strcpy(u->site,nl->service);
sprintf(text,"%s enters from cyberspace.\n",u->name);
write_room(nl->connect_room,text);
sprintf(text,"NETLINK: Remote user %s received from %s.\n",u->name,nl->service);
write_syslog(text,1,TOSYS);
u->room=nl->connect_room;
u->netlink=nl;
u->read_mail=time(0);
u->last_login=time(0);
num_of_users++;
sprintf(text,"GRANTED %s\n",name);
write_sock(nl->socket,text);
}
		

/*** User is leaving this system ***/
nl_release(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if ((u=get_user(name))!=NULL && u->type==REMOTE_TYPE) {
	sprintf(text,"%s leaves this plain of existence.\n",u->name);
	write_room_except(u->room,text,u);
	sprintf(text,"NETLINK: Remote user %s released.\n",u->name);
	write_syslog(text,1,TOSYS);
	destroy_user_clones(u);
	destruct_user(u);
	num_of_users--;
	return;
	}
sprintf(text,"NETLINK: Release requested for unknown/invalid user %s from %s.\n",name,nl->service);
write_syslog(text,1,TOSYS);
}


/*** Remote user performs an action on this system ***/
nl_action(nl,name,inpstr)
NL_OBJECT nl;
char *name,*inpstr;
{
UR_OBJECT u;
char *c,ctemp;

if (!(u=get_user(name))) {
	sprintf(text,"DENIED %s 8\n",name);
	write_sock(nl->socket,text);
	return;
	}
if (u->socket!=-1) {
	sprintf(text,"NETLINK: Action requested for local user %s from %s.\n",name,nl->service);
	write_syslog(text,1,TOSYS);
	return;
	}
inpstr=remove_first(remove_first(inpstr));
/* remove newline character */
c=inpstr; ctemp='\0';
while(*c) {
	if (*c=='\n') {  ctemp=*c;  *c='\0';  break;  }
	++c;
	}
u->last_input=time(0);
if (u->misc_op) {
	if (!strcmp(inpstr,"NL")) misc_ops(u,"\n");  
	else misc_ops(u,inpstr+4);
	return;
	}
if (u->afk) {
	write_user(u,"You are no longer AFK.\n");  
	if (u->vis) {
		sprintf(text,"%s comes back from being AFK.\n",u->name);
		write_room_except(u->room,text,u);
		}
	u->afk=0;
	}
word_count=wordfind(inpstr);
if (!strcmp(inpstr,"NL")) return; 
exec_com(u,inpstr);
if (ctemp) *c=ctemp;
if (!u->misc_op) prompt(u);
}


/*** Grant received from remote system ***/
nl_granted(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;
RM_OBJECT old_room;

if (!strcmp(name,"CONNECT")) {
	sprintf(text,"NETLINK: Connection to %s granted.\n",nl->service);
	write_syslog(text,1,TOSYS);
	/* Send our verification and version number */
	sprintf(text,"VERIFICATION %s %s\n",verification,VERSION);
	write_sock(nl->socket,text);
	return;
	}
if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Grant received for unknown user %s from %s.\n",name,nl->service);
	write_syslog(text,1,TOSYS);
	return;
	}
/* This will probably occur if a user tried to go to the other site , got 
   lagged then changed his mind and went elsewhere. Don't worry about it. */
if (u->remote_com!=GO) {
	sprintf(text,"NETLINK: Unexpected grant for %s received from %s.\n",name,nl->service);
	write_syslog(text,1,TOSYS);
	return;
	}
/* User has been granted permission to move into remote talker */
write_user(u,"~FB~OLYou traverse cyberspace...\n");
if (u->vis) {
	sprintf(text,"%s %s to the %s.\n",u->name,u->out_phrase,nl->service);
	write_room_except(u->room,text,u);
	}
else write_room_except(u->room,invisleave,u);
sprintf(text,"NETLINK: %s transfered to %s.\n",u->name,nl->service);
write_syslog(text,1,TOSYS);
old_room=u->room;
u->room=NULL; /* Means on remote talker */
u->netlink=nl;
u->pot_netlink=NULL;
u->remote_com=-1;
u->misc_op=0;  
u->filepos=0;  
u->page_file[0]='\0';
reset_access(old_room);
sprintf(text,"ACT %s look\n",u->name);
write_sock(nl->socket,text);
}


/*** Deny received from remote system ***/
nl_denied(nl,name,inpstr)
NL_OBJECT nl;
char *name,*inpstr;
{
UR_OBJECT u,create_user();
int errnum;
char *neterr[]={
"this site is not in the remote services valid sites list",
"the remote service is unable to create a link",
"the remote service has no free room links",
"the link is for incoming users only",
"a user with your name is already logged on the remote site",
"the remote service was unable to create a session for you",
"incorrect password. Use '.go <service> <remote password>'",
"your level there is below the remote services current minlogin level",
"you are banned from that service"
};

errnum=0;
sscanf(remove_first(remove_first(inpstr)),"%d",&errnum);
if (!strcmp(name,"CONNECT")) {
	sprintf(text,"NETLINK: Connection to %s denied, %s.\n",nl->service,neterr[errnum-1]);
	write_syslog(text,1,TOSYS);
	/* If wiz initiated connect let them know its failed */
	sprintf(text,"~OLSYSTEM:~RS Connection to %s failed, %s.\n",nl->service,neterr[errnum-1]);
	write_level(com_level[CONN],1,text,NULL);
	close(nl->socket);
	nl->type=UNCONNECTED;
	nl->stage=DOWN;
	return;
	}
/* Is for a user */
if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Deny for unknown user %s received from %s.\n",name,nl->service);
	write_syslog(text,1,TOSYS);
	return;
	}
sprintf(text,"NETLINK: Deny %d for user %s received from %s.\n",errnum,name,nl->service);
write_syslog(text,1,TOSYS);
sprintf(text,"Sorry, %s.\n",neterr[errnum-1]);
write_user(u,text);
prompt(u);
u->remote_com=-1;
u->pot_netlink=NULL;
}


/*** Text received to display to a user on here ***/
nl_mesg(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Message received for unknown user %s from %s.\n",name,nl->service);
	write_syslog(text,1,TOSYS);
	nl->mesg_user=(UR_OBJECT)-1;
	return;
	}
nl->mesg_user=u;
}


/*** Remote system asking for prompt to be displayed ***/
nl_prompt(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Prompt received for unknown user %s from %s.\n",name,nl->service);
	write_syslog(text,1,TOSYS);
	return;
	}
if (u->type==REMOTE_TYPE) {
	sprintf(text,"NETLINK: Prompt received for remote user %s from %s.\n",name,nl->service);
	write_syslog(text,1,TOSYS);
	return;
	}
prompt(u);
}


/*** Verification received from remote site ***/
nl_verification(nl,w2,w3,com)
NL_OBJECT nl;
char *w2,*w3;
int com;
{
NL_OBJECT nl2;
char tvar[ROOM_NAME_LEN+1];


if (!com) {
	/* We're verifiying a remote site */
	if (!w2[0]) {
		shutdown_netlink(nl);  return;
		}
	for(nl2=nl_first;nl2!=NULL;nl2=nl2->next) {
		if (!strcmp(nl->site,nl2->site) && !strcmp(w2,nl2->verification)) {
			switch(nl->allow) {
				case IN : write_sock(nl->socket,"VERIFY OK IN\n");  break;
				case OUT: write_sock(nl->socket,"VERIFY OK OUT\n");  break;
				case ALL: write_sock(nl->socket,"VERIFY OK ALL\n"); 
				}
			strcpy(nl->service,nl2->service);

			/* Only 3.2.0 and above send version number with verification */
			sscanf(w3,"%d.%d.%d",&nl->ver_major,&nl->ver_minor,&nl->ver_patch);
			sprintf(text,"NETLINK: Connected to %s in the %s.\n",nl->service,nl->connect_room->name);
			write_syslog(text,1,TOSYS);
			strcpy(tvar,nl->connect_room->name);
			delump(tvar);

			sprintf(text,"~OLSYSTEM:~RS New connection to service %s in the %s.\n",nl->service,tvar);
			write_room(NULL,text);
			return;
			}
		}
	write_sock(nl->socket,"VERIFY BAD\n");
	shutdown_netlink(nl);
	return;
	}
/* The remote site has verified us */
if (!strcmp(w2,"OK")) {
	/* Set link permissions */
	if (!strcmp(w3,"OUT")) {
		if (nl->allow==OUT) {
			sprintf(text,"NETLINK: WARNING - Permissions deadlock, both sides are outgoing only.\n");
			write_syslog(text,1,TOSYS);
			}
		else nl->allow=IN;
		}
	else {
		if (!strcmp(w3,"IN")) {
			if (nl->allow==IN) {
				sprintf(text,"NETLINK: WARNING - Permissions deadlock, both sides are incoming only.\n");
				write_syslog(text,1,TOSYS);
				}
			else nl->allow=OUT;
			}
		}
	sprintf(text,"NETLINK: Connection to %s verified.\n",nl->service);
	write_syslog(text,1,TOSYS);
	strcpy(tvar,nl->connect_room->name);
	delump(tvar);
	sprintf(text,"~OLSYSTEM:~RS New connection to service %s in the %s.\n",nl->service,tvar);
	write_room(NULL,text);
	return;
	}
if (!strcmp(w2,"BAD")) {
	sprintf(text,"NETLINK: Connection to %s has bad verification.\n",nl->service);
	write_syslog(text,1,TOSYS);
	/* Let wizes know its failed , may be wiz initiated */
	sprintf(text,"~OLSYSTEM:~RS Connection to %s failed, bad verification.\n",nl->service);
	write_level(com_level[CONN],1,text,NULL);
	shutdown_netlink(nl);  
	return;
	}
sprintf(text,"NETLINK: Unknown verify return code from %s.\n",nl->service);
write_syslog(text,1,TOSYS);
shutdown_netlink(nl);
}


/* Remote site only sends REMVD (removed) notification if user on remote site 
   tries to .go back to his home site or user is booted off. Home site doesn't
   bother sending reply since remote site will remove user no matter what. */
nl_removed(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Removed notification for unknown user %s received from %s.\n",name,nl->service);
	write_syslog(text,1,TOSYS);
	return;
	}
if (u->room!=NULL) {
	sprintf(text,"NETLINK: Removed notification of local user %s received from %s.\n",name,nl->service);
	write_syslog(text,1,TOSYS);
	return;
	}
sprintf(text,"NETLINK: %s returned from %s.\n",u->name,u->netlink->service);
write_syslog(text,1,TOSYS);
u->room=u->netlink->connect_room;
u->netlink=NULL;
if (u->vis) {
	sprintf(text,"%s %s\n",u->name,u->in_phrase);
	write_room_except(u->room,text,u);
	}
else write_room_except(u->room,invisenter,u);
look(u,0);
prompt(u);
}


/*** Got an error back from site, deal with it ***/
nl_error(nl)
NL_OBJECT nl;
{
if (nl->mesg_user!=NULL) nl->mesg_user=NULL;
/* lastcom value may be misleading, the talker may have sent off a whole load
   of commands before it gets a response due to lag, any one of them could
   have caused the error */
sprintf(text,"NETLINK: Received ERROR from %s, lastcom = %d.\n",nl->service,nl->lastcom);
write_syslog(text,1,TOSYS);
}


/*** Does user exist? This is a question sent by a remote mailer to
     verifiy mail id's. ***/
nl_checkexist(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
FILE *fp;
char filename[80];

sprintf(filename,"%s/%s.D",USERFILES,to);
if (!(fp=fopen(filename,"r"))) {
	sprintf(text,"EXISTS_NO %s %s\n",to,from);
	write_sock(nl->socket,text);
	return;
	}
fclose(fp);
sprintf(text,"EXISTS_YES %s %s\n",to,from);
write_sock(nl->socket,text);
}


/*** Remote user doesnt exist ***/
nl_user_notexist(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
UR_OBJECT user;
char filename[80];
char text2[ARR_SIZE];

if ((user=get_user(from))!=NULL) {
	sprintf(text,"~OLSYSTEM:~RS User %s does not exist at %s, your mail bounced.\n",to,nl->service);
	write_user(user,text);
	}
else {
	sprintf(text2,"There is no user named %s at %s, your mail bounced.\n",to,nl->service);
	send_mail(NULL,from,text2);
	}
sprintf(filename,"%s/OUT_%s_%s@%s",MAILSPOOL,from,to,nl->service);
unlink(filename);
}


/*** Remote users exists, send him some mail ***/
nl_user_exist(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
UR_OBJECT user;
FILE *fp;
char text2[ARR_SIZE],filename[80],line[82];

sprintf(filename,"%s/OUT_%s_%s@%s",MAILSPOOL,from,to,nl->service);
if (!(fp=fopen(filename,"r"))) {
	if ((user=get_user(from))!=NULL) {
		sprintf(text,"~OLSYSTEM:~RS An error occured during mail delivery to %s@%s.\n",to,nl->service);
		write_user(user,text);
		}
	else {
		sprintf(text2,"An error occured during mail delivery to %s@%s.\n",to,nl->service);
		send_mail(NULL,from,text2);
		}
	return;
	}
sprintf(text,"MAIL %s %s\n",to,from);
write_sock(nl->socket,text);
fgets(line,80,fp);
while(!feof(fp)) {
	write_sock(nl->socket,line);
	fgets(line,80,fp);
	}
fclose(fp);
write_sock(nl->socket,"\nENDMAIL\n");
unlink(filename);
}


/*** Got some mail coming in ***/
nl_mail(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
char filename[80];

sprintf(text,"NETLINK: Mail received for %s from %s.\n",to,nl->service);
write_syslog(text,1,TOSYS);
sprintf(filename,"%s/IN_%s_%s@%s",MAILSPOOL,to,from,nl->service);
if (!(nl->mailfile=fopen(filename,"w"))) {
	sprintf(text,"ERROR: Couldn't open file %s to write in nl_mail().\n",filename);
	write_syslog(text,0,TOSYS);
	sprintf(text,"MAILERROR %s %s\n",to,from);
	write_sock(nl->socket,text);
	return;
	}
strcpy(nl->mail_to,to);
strcpy(nl->mail_from,from);
}


/*** End of mail message being sent from remote site ***/
nl_endmail(nl)
NL_OBJECT nl;
{
FILE *infp,*outfp;
char c,infile[80],mailfile[80],line[DNL+1];

fclose(nl->mailfile);
nl->mailfile=NULL;

sprintf(mailfile,"%s/IN_%s_%s@%s",MAILSPOOL,nl->mail_to,nl->mail_from,nl->service);

/* Copy to users mail file to a tempfile */
if (!(outfp=fopen("tempfile","w"))) {
	write_syslog("ERROR: Couldn't open tempfile in netlink_endmail().\n",0,TOSYS);
	sprintf(text,"MAILERROR %s %s\n",nl->mail_to,nl->mail_from);
	write_sock(nl->socket,text);
	goto END;
	}
fprintf(outfp,"%d\r",(int)time(0));

/* Copy old mail file to tempfile */
sprintf(infile,"%s/%s.M",USERFILES,nl->mail_to);
if (!(infp=fopen(infile,"r"))) goto SKIP;
fgets(line,DNL,infp);
c=getc(infp);
while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }
fclose(infp);

/* Copy received file */
SKIP:
if (!(infp=fopen(mailfile,"r"))) {
	sprintf(text,"ERROR: Couldn't open file %s to read in netlink_endmail().\n",mailfile);
	write_syslog(text,0,TOSYS);
	sprintf(text,"MAILERROR %s %s\n",nl->mail_to,nl->mail_from);
	write_sock(nl->socket,text);
	goto END;
	}
fprintf(outfp,"~OLFrom: %s@%s  %s",nl->mail_from,nl->service,long_date(0));
c=getc(infp);
while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }
fclose(infp);
fclose(outfp);
rename("tempfile",infile);
write_user(get_user(nl->mail_to),"\07~FT~OL~LI** YOU HAVE NEW MAIL **\n");

END:
nl->mail_to[0]='\0';
nl->mail_from[0]='\0';
unlink(mailfile);
}


/*** An error occured at remote site ***/
nl_mailerror(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
UR_OBJECT user;

if ((user=get_user(from))!=NULL) {
	sprintf(text,"~OLSYSTEM:~RS An error occured during mail delivery to %s@%s.\n",to,nl->service);
	write_user(user,text);
	}
else {
	sprintf(text,"An error occured during mail delivery to %s@%s.\n",to,nl->service);
	send_mail(NULL,from,text);
	}
}


/*** Send statistics of this server to requesting user on remote site ***/
nl_rstat(nl,to)
NL_OBJECT nl;
char *to;
{
char str[80];

gethostname(str,80);
if (nl->ver_major<=3 && nl->ver_minor<2)
	sprintf(text,"MSG %s\n\n*** Remote statistics ***\n\n",to);
else sprintf(text,"MSG %s\n\n~BB*** Remote statistics ***~RS\n\n",to);
write_sock(nl->socket,text);
sprintf(text,"NUTS version         : %s\nHost                 : %s\n",VERSION,str);
write_sock(nl->socket,text);
sprintf(text,"Ports (Main/Wiz/Link): %d ,%d, %d\n",port[0],port[1],port[2]);
write_sock(nl->socket,text);
sprintf(text,"Number of users      : %d\nRemote user maxlevel : %s\n",num_of_users,level_name[0][rem_user_maxlevel]);
write_sock(nl->socket,text);
sprintf(text,"Remote user deflevel : %s\n\nEMSG\nPRM %s\n",level_name[0][rem_user_deflevel],to);
write_sock(nl->socket,text);
}


/*** Shutdown the netlink and pull any remote users back home ***/
shutdown_netlink(nl)
NL_OBJECT nl;
{
UR_OBJECT u;
char mailfile[80];

if (nl->type==UNCONNECTED) return;

/* See if any mail halfway through being sent */
if (nl->mail_to[0]) {
	sprintf(text,"MAILERROR %s %s\n",nl->mail_to,nl->mail_from);
	write_sock(nl->socket,text);
	fclose(nl->mailfile);
	sprintf(mailfile,"%s/IN_%s_%s@%s",MAILSPOOL,nl->mail_to,nl->mail_from,nl->service);
	unlink(mailfile);
	nl->mail_to[0]='\0';
	nl->mail_from[0]='\0';
	}
write_sock(nl->socket,"DISCONNECT\n");
close(nl->socket);  
for(u=user_first;u!=NULL;u=u->next) {
	if (u->pot_netlink==nl) {  u->remote_com=-1;  continue;  }
	if (u->netlink==nl) {
		if (u->room==NULL) {
			write_user(u,"~FB~OLYou feel yourself dragged back across the ether...\n");
			u->room=u->netlink->connect_room;
			u->netlink=NULL;
			if (u->vis) {
				sprintf(text,"%s %s\n",u->name,u->in_phrase);
				write_room_except(u->room,text,u);
				}
			else write_room_except(u->room,invisenter,u);
			look(u,0);  prompt(u);
			sprintf(text,"NETLINK: %s recovered from %s.\n",u->name,nl->service);
			write_syslog(text,1,TOSYS);
			continue;
			}
		if (u->type==REMOTE_TYPE) {
			sprintf(text,"%s vanishes!\n",u->name);
			write_room(u->room,text);
			destruct_user(u);
			num_of_users--;
			}
		}
	}
if (nl->stage==UP) 
	sprintf(text,"NETLINK: Disconnected from %s.\n",nl->service);
else sprintf(text,"NETLINK: Disconnected from site %s.\n",nl->site);
write_syslog(text,1,TOSYS);
if (nl->type==INCOMING) {
	nl->connect_room->netlink=NULL;
	destruct_netlink(nl);  
	return;
	}
nl->type=UNCONNECTED;
nl->stage=DOWN;
nl->warned=0;
}



/*************** START OF COMMAND FUNCTIONS AND THEIR SUBSIDS **************/

/*** Deal with user input ***/
exec_com(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int i,len,ret,syntax;
char filename[80],*comword=NULL;
UR_OBJECT u;
CH_OBJECT ch;

com_num=-1;
if (word[0][0]=='.') comword=(word[0]+1);
else comword=word[0];
if (!comword[0]) {
	write_user(user,"Unknown command.\n");  return;
	}

/* get com_num */
if (!strcmp(word[0],">")) strcpy(word[0],"tell");
if (!strcmp(word[0],"<")) strcpy(word[0],"pemote");
if (!strcmp(word[0],"-")) strcpy(word[0],"echo");
if (!strcmp(word[0],"+")) strcpy(word[0],"sto");
if (inpstr[0]==';') strcpy(word[0],"emote");
	else inpstr=remove_first(inpstr);

i=0;
len=strlen(comword);
while(command[i][0]!='*') {
	if (!strncmp(command[i],comword,len)) {  com_num=i;  break;  }
	++i;
	}

/* NEWADD */
if (com_num==-1 && user->room!=NULL) {
	for (ch=ch_first;ch!=NULL;ch=ch->next) 
		if (!strcmp(ch->name,comword)) {
			shout(user,inpstr,ch);
			return;
			}

	if ((ret=search_for_macro(user,comword,0,&syntax))!=-1) {
		user->filepos=ret;
		if (!syntax) {
			if (!word[1][0]) {
				write_user(user,"This macro needs a target\n");
				user->filepos=0;
				return;
				}		
			if (!(u=get_user(word[1]))) {
				write_user(user,notloggedon);	
				user->filepos=0;
				return;
				}
			strcpy(user->macro,u->name);
			}
		else strcpy(user->macro,user->name);
		run_macro(user,0);
		return;
		}
	if ((ret=search_for_macro(user,comword,1,&syntax))!=-1) {
		user->filepos=ret;
		if (!syntax) {
			if (!word[1][0]) {
				write_user(user,"This macro needs a target\n");
				user->filepos=0;
				return;
				}		
			if (!(u=get_user(word[1]))) {
				write_user(user,notloggedon);	
				user->filepos=0;
				return;
				}
			strcpy(user->macro,u->name);
			}
		else strcpy(user->macro,user->name);
		run_macro(user,1);
		return;
		}
	}
/*
	la funzione macro va a cercare nei file User.macro e
	datafiles/gmacro il seek di filepos della macro in questione.
	quando lo individuano, ritornano un risultato non nullo e le macro
	sono eseguite come comandi. Se invece ritornano 0, il programma
	prosegue, e fornisce unknown command.
	E' importante notare che prima cerca tra i comandi, quindi tra
	le macro personali, infine tra quelle globali.
*/


if (user->room!=NULL && (com_num==-1 || com_level[com_num] > user->level)) {
	write_user(user,"Unknown command.\n");  return;
	}

/* See if user has gone across a netlink and if so then intercept certain 
   commands to be run on home site */
if (user->room==NULL) {
	switch(com_num) {
		case HOME: 
		case QUIT:
		case MODE:
		case PROMPT: 
		case COLOUR:
		case REBOOT:
		case SUICIDE:
		case SHUTDOWN: 
		case CHARECHO:
		write_user(user,"~FY~OL*** Home execution ***\n");  break;

		default:
		sprintf(text,"ACT %s %s %s\n",user->name,word[0],inpstr);
		write_sock(user->netlink->socket,text);
		no_prompt=1;
		return;
		}
	}
/* Dont want certain commands executed by remote users */
if (user->type==REMOTE_TYPE) {
	switch(com_num) {
		case PASSWD :
		case ENTPRO :
		case ASPECT :
		case ACCREQ :
		case CONN   :
		case DISCONN:
			write_user(user,"Sorry, remote users cannot use that command.\n");
			return;
		}
	}

/* Main switch */
switch(com_num) {
	case QUIT: disconnect_user(user);  break;
	case LOOK: look(user,1);  break;
	case MODE: toggle_mode(user);  break;
	case SAY : 
		if (word_count<2) {
			write_user(user,"Say what?\n");  return;
			}
		say(user,inpstr);
		break;
	case TELL  : tell(user,inpstr);   break;
	case EMOTE : emote(user,inpstr);  break;
	case PEMOTE: pemote(user,inpstr); break;
	case ECHO  : echo(user,inpstr);   break;
	case GO    : go(user,inpstr);  break;
	case IGNALL: toggle_ignall(user);  break;
	case PROMPT: toggle_prompt(user);  break;
	case DESC  : set_desc(user,inpstr);  break;
	case INPHRASE : 
	case OUTPHRASE: 
		set_iophrase(user,inpstr);  break; 
	case PUBCOM :
	case PRIVCOM: set_room_access(user,inpstr);  break;
	case LETMEIN: letmein(user,inpstr);  break;
	case INVITE : invite(user);   break;
	case TOPIC  : set_topic(user,inpstr);  break;
	case MOVE   : move(user,inpstr);  break;
	case BCAST  : bcast(user,inpstr);  break;
	case WHO    : who(user,0);  break;
	case PEOPLE : who(user,1);  break;
	case HELP   : help(user);  break;
	case SHUTDOWN: shutdown_com(user);  break;
	case NEWS:
		sprintf(filename,"%s/%s",DATAFILES,NEWSFILE);
		switch(more(user,user->socket,filename)) {
			case 0: write_user(user,"There is no news.\n");  break;
			case 1: user->misc_op=2;
			}
		break;
	case READ  : read_board(user,inpstr);  break;
	case WRITE : write_board(user,inpstr,0);  break;
	case WIPE  : wipe_board(user);  break;
	case SEARCH: search_boards(user);  break;
	case REVIEW: review(user,inpstr);  break;
	case HOME  : home(user);  break;
	case STATUS: status(user);  break;
	case VER:
		sprintf(text,"NUTS version %s\n",VERSION);
		write_user(user,text);  break;
	case RMAIL   : rmail(user);  break;
	case SMAIL   : smail(user,inpstr,0);  break;
	case DMAIL   : dmail(user);  break;
	case FROM    : mail_from(user);  break;
	case ENTPRO  : enter_profile(user,0);  break;
	case EXAMINE : examine(user);  break;
	case RMST    : rooms(user,1);  break;
	case RMSN    : rooms(user,0);  break;
	case NETSTAT : netstat(user);  break;
	case NETDATA : netdata(user);  break;
	case CONN    : connect_netlink(user,inpstr);  break;
	case DISCONN : disconnect_netlink(user,inpstr);  break;
	case PASSWD  : change_pass(user);  break;
	case KILL    : kill_user(user);  break;
	case PROMOTE : promote(user);  break;
	case DEMOTE  : demote(user);  break;
	case LISTBANS: listbans(user);  break;
	case BAN     : ban(user);  break;
	case UNBAN   : unban(user);  break;
	case VIS     : visibility(user,1);  break;
	case INVIS   : visibility(user,0);  break;
	case SITE    : site(user);  break;
	case WAKE    : wake(user);  break;
	case WIZSHOUT: wizshout(user,inpstr);  break;
	case MUZZLE  : muzzle(user);  break;
	case UNMUZZLE: unmuzzle(user);  break;
	case MAP: 
		sprintf(filename,"%s/%s%s",DATAFILES,MAPFILE,user->room->maptype);
		switch(more(user,user->socket,filename)) {
			case 0: write_user(user,"There is no map.\n");  break;
			case 1: user->misc_op=2;
			}
		break;
	case LOGGING  : logging(user); break;
	case MINLOGIN : minlogin(user);  break;
	case SYSTEM   : system_details(user);  break;
	case CHARECHO : toggle_charecho(user);  break;
	case CLEARLINE: clearline(user);  break;
	case FIX      : change_room_fix(user,1,inpstr);  break;
	case UNFIX    : change_room_fix(user,0,inpstr);  break;
	case VIEWLOG  : viewlog(user);  break;
	case ACCREQ   : account_request(user);  break;
	case REVCLR   : revclr(user);  break;
/*
	case CREATE   : create_clone(user,inpstr);  break;
	case DESTROY  : destroy_clone(user);  break;
	case MYCLONES : myclones(user);  break;
	case ALLCLONES: allclones(user);  break;
	case SWITCH: clone_switch(user);  break;
	case CSAY  : clone_say(user,inpstr);  break;
	case CHEAR : clone_hear(user);  break;
*/
	case RSTAT : remote_stat(user,inpstr);  break;
	case SWBAN : swban(user);  break;
	case AFK   : afk(user,inpstr);  break;
	case CLS   : cls(user);  break;
	case COLOUR  : toggle_colour(user);  break;
	case IGNTELL : toggle_igntell(user);  break;
	case SUICIDE : suicide(user);  break;
	case DELETE  : delete_user(user,0);  break;
	case REBOOT  : reboot_com(user);  break;
	case RECOUNT : check_messages(user,2);  break;
	case REVTELL : revtell(user);  break;
	case STO      : to(user,inpstr); break;
	case DOC     : document(user); break;
	case ROOM    : room_opt(user,inpstr); break;
	case PATH    : path(user); break;
	case LEVEL   : level_list(user); break;
	case HULK     : hulk(user); break;
	case UNDO     : undo(user); break;
	case ASPECT   : aspect(user,0); break;
	case JOIN     : join(user); break;
	case MACRO    : macro(user); break;
	case SEE      :
	case SEND     : send_banner(user); break;
	default: write_user(user,"Command not executed in exec_com().\n");
	}	
}



/*** Display details of room ***/
look(user,direct_call)
UR_OBJECT user;
int direct_call;
{
RM_OBJECT rm;
UR_OBJECT u;
char temp[81],null[1],*ptr;
char *afk="~BR(AFK)";
int i,exits,users;
char tvar[ROOM_NAME_LEN+1];
char filename[80];
int ret;

if (word[1][0] && direct_call) {
	if (u=get_user(word[1])) {
		if (u->room==user->room) {
			sprintf(filename,"%s/%s.L",USERFILES,u->name);
			if (!(ret=more(user,user->socket,filename)))
				write_user(user,"Non vedi niente di particolarmente rilevante\n");
			else if (ret==1) user->misc_op=2;
			if (u==user) {
				sprintf(text,"%s si osserva attentamente...\n",user->name);
				write_room_except(user->room,text,user);
				return;
				}
			sprintf(text,"%s ti scruta attentamente...\n",user->name);
			write_user(u,text);
			sprintf(text,"%s scruta attentamente %s\n",user->name,u->name);
			write_room_except2(user->room,text,user,u);
			return;
			}
		else {
			write_user(user,"Non e' qui al momento\n");
			return;
		}
	}
	else { 
		write_user(user,notloggedon); 
		return;
	}
}


rm=user->room;
strcpy(tvar,rm->name);
delump(tvar);


if (rm->access & PRIVATE) sprintf(text,"\n~FTRoom: ~FR%s\n\n",tvar);
else sprintf(text,"\n~FTRoom: ~FG%s\n\n",tvar);
write_user(user,text);

sprintf(filename,"%s/%s.R",DATAFILES,rm->name);

if(!more(user,user->socket,filename)) 
	write_user(user,"Sei avvolto da una fitta oscurita'.\n");


exits=0;  null[0]='\0';
strcpy(text,"\n~FTExits are:");
for(i=0;i<MAX_LINKS;++i) {
	if (rm->link[i]==NULL) break;
	strcpy(tvar,rm->link[i]->name);
	delump(tvar);
	if (rm->link[i]->access & PRIVATE) 
		sprintf(temp,"  ~FR%s",tvar);
	else sprintf(temp,"  ~FG%s",tvar);
	strcat(text,temp);
	++exits;
	}
if (rm->netlink!=NULL && rm->netlink->stage==UP) {
	if (rm->netlink->allow==IN) sprintf(temp,"  ~FR%s*",rm->netlink->service);
	else sprintf(temp,"  ~FG%s*",rm->netlink->service);
	strcat(text,temp);
	}
else if (!exits) strcpy(text,"\n~FTThere are no exits.");
strcat(text,"\n\n");
write_user(user,text);

users=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->room!=rm || u==user || (!u->vis && u->level>user->level)) 
		continue;
	if (!users++) write_user(user,"~FTYou can see:\n");
	if (u->afk) ptr=afk; else ptr=null;
	if (!u->vis) sprintf(text,"     ~FR*~RS%s %s~RS  %s\n",u->name,u->desc,ptr);
	else sprintf(text,"      %s %s~RS  %s\n",u->name,u->desc,ptr);
	write_user(user,text);
	}
if (!users) write_user(user,"~FTYou are all alone here.\n");
write_user(user,"\n");

strcpy(text,"Access is ");
switch(rm->access) {
	case PUBLIC:  strcat(text,"set to ~FGPUBLIC~RS");  break;
	case PRIVATE: strcat(text,"set to ~FRPRIVATE~RS");  break;
	case FIXED_PUBLIC:  strcat(text,"~FRfixed~RS to ~FGPUBLIC~RS");  break;
	case FIXED_PRIVATE: strcat(text,"~FRfixed~RS to ~FRPRIVATE~RS");  break;
	}
sprintf(temp," and there are ~OL~FM%d~RS messages on the board.\n",rm->mesg_cnt);
strcat(text,temp);
write_user(user,text);
if (rm->topic[0]) {
	sprintf(text,"Current topic: %s\n",rm->topic);
	write_user(user,text);
	return;
	}
write_user(user,"No topic has been set yet.\n");	
}



/*** Switch between command and speech mode ***/
toggle_mode(user)
UR_OBJECT user;
{
if (user->command_mode) {
	write_user(user,"Now in SPEECH mode.\n");
	user->command_mode=0;  return;
	}
write_user(user,"Now in COMMAND mode.\n");
user->command_mode=1;
}


/*** Shutdown the talker ***/
talker_shutdown(user,str,reboot)
UR_OBJECT user;
char *str;
int reboot;
{
UR_OBJECT u;
NL_OBJECT nl;
int i;
char *ptr;
char *args[]={ progname,confile,NULL };

if (user!=NULL) ptr=user->name; else ptr=str;
if (reboot) {
	write_room(NULL,"\07\n~OLSYSTEM:~FY~LI Rebooting now!!\n\n");
	sprintf(text,"*** REBOOT initiated by %s ***\n",ptr);
	}
else {
	write_room(NULL,"\07\n~OLSYSTEM:~FR~LI Shutting down now!!\n\n");
	sprintf(text,"*** SHUTDOWN initiated by %s ***\n",ptr);
	}
write_syslog(text,0,TOSYS);
for(nl=nl_first;nl!=NULL;nl=nl->next) shutdown_netlink(nl);
for(u=user_first;u!=NULL;u=u->next) disconnect_user(u);
for(i=0;i<3;++i) close(listen_sock[i]); 
if (reboot) {
	/* If someone has changed the binary or the config filename while this 
	   prog has been running this won't work */
	execvp(progname,args);
	/* If we get this far it hasn't worked */
	sprintf(text,"*** REBOOT FAILED %s: %s ***\n\n",long_date(1),sys_errlist[errno]);
	write_syslog(text,0,TOSYS);
	exit(12);
	}
sprintf(text,"*** SHUTDOWN complete %s ***\n\n",long_date(1));
write_syslog(text,0,TOSYS);
exit(0);
}


/*** Say user speech. ***/
say(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char type[10],*name;
char emotion[80];
int emnum,check;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot speak.\n");  return;
	}
if (user->type==REMOTE_TYPE) {

	if (!(strcmp(word[1],"+"))) {
		inpstr=remove_first(inpstr);
		strcpy(word[1],word[2]);
		to(user,inpstr);
		return;
		}

}


		

if (user->room==NULL) {
	sprintf(text,"ACT %s say %s\n",user->name,inpstr);
	write_sock(user->netlink->socket,text);
	no_prompt=1;
	return;
	}
if (word_count<2 && user->command_mode) {
	write_user(user,"Say what?\n");  return;
	}
switch(inpstr[strlen(inpstr)-1]) {
     case '?': strcpy(type,"ask");  break;
     case '!': strcpy(type,"exclaim");  break;
     default : strcpy(type,"say");
     }
if (user->type==CLONE_TYPE) {
	sprintf(text,"Clone of %s %ss: %s\n",user->name,type,inpstr);
	write_room(user->room,text);
	record(user->room,text);
	return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}

if (user->command_mode) check=1;
	else check=0;

if ((emnum=get_emotion(word[check]))!=-1 && word[check+1][0]) {
	strcpy(emotion,emotions_array[emnum]);
	inpstr=remove_first(inpstr);
	sprintf(text,"%s, you %s: %s\n",emotion,type,inpstr);
	write_user(user,text);
	if (user->vis) name=user->name; else name=invisname;
	sprintf(text,"%s, %s %ss: %s\n",emotion,name,type,inpstr);
	write_room_except(user->room,text,user);
	}
else {
	sprintf(text,"You %s: %s\n",type,inpstr);
	write_user(user,text);
	if (user->vis) name=user->name; else name=invisname;
	sprintf(text,"%s %ss: %s\n",name,type,inpstr);
	write_room_except(user->room,text,user);
	}
record(user->room,text);
}


/*** Shout something ***/
shout(user,inpstr,channel)
UR_OBJECT user;
char *inpstr;
CH_OBJECT channel;
{
UR_OBJECT u;
char *name,*pun,*pun2;
int i,cnt,line;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot shout in a channel.\n"); return;
	}
if (word_count<2) {
	sprintf(text,"Channel %s:\n.%s <phrase> shout in the channel\n",channel->long_name,channel->name);
	write_user(user,text);
	sprintf(text,".%s ; <phrase> emote in the channel\n.%s + <user name> <phrase> shout to user in the channel\n",channel->name,channel->name);
	write_user(user,text);  
	sprintf(text,".%s -j join to the channel\n.%s -u unjoin the channel\n",channel->name,channel->name);
	write_user(user,text);  
	sprintf(text,".%s -l list the logged user who are connected to the channel\n",channel->name);
	write_user(user,text);
	sprintf(text,".%s -r review the buffer of the shout channel\n",channel->name);
	write_user(user,text);
	sprintf(text,".%s -c delete the buffer of the shout channel\n",channel->name);
	write_user(user,text);
	for (i=0;i<MAX_USER_CHANNEL;++i)
		if (user->channel[i]==channel) break;
	if (i==MAX_USER_CHANNEL) 
		write_user(user,"Joined to the channel : NO\n");
	else
		write_user(user,"Joined to the channel : YES\n"); 
	return;
	}

	if (word[1][0]=='-' && word[1][1]=='j') {
		for(i=0;i<MAX_USER_CHANNEL;++i) {
			if (user->channel[i]==channel) {
				write_user(user,"You are already joined to this channel\n");
				return;
				}
			if (user->channel[i]==NULL) break;
			}
		if (i==MAX_USER_CHANNEL) {
        		write_user(user,"Sorry... no more channel free... Drop some channels to join a new one\n");
        		return;
		        }
		user->channel[i]=channel;
		if (user->vis) name=user->name; else name=invisname;
		substitute(text,channel->join,name,"\0",channel->long_name);
		strcat(text,"\n");
		write_to_listener_users(text,channel);
		record_shout(text,channel);
		return;
		}

	if (word[1][0]=='-' && word[1][1]=='l') {
		write_user(user,"Listener of this channel :\n\n");
		for(u=user_first;u!=NULL;u=u->next) {
			for (i=0;i<MAX_USER_CHANNEL;++i) {
				if (u->channel[i]==channel) {
					sprintf(text,"%s\n",u->name);
					write_user(user,text);
					}
				}
			}
		return;
		}

for (i=0;i<MAX_USER_CHANNEL;++i)
	if (user->channel[i]==channel) break;
if (i==MAX_USER_CHANNEL) {
	sprintf(text,"You are not joined to this channel...\nUse\n%s -j\nto join it\n",channel->name);
	write_user(user,text);
	return;
	}

if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}

if (word[1][0]==';') {
	inpstr=remove_first(inpstr);
	if (user->vis) name=user->name; else name=invisname;
	sprintf(text,"[Canale ~OL%s~RS] %s %s\n",channel->long_name,name,inpstr);
	write_to_listener_users(text,channel);
	record_shout(text,channel);
	return;
	}

if (word[1][0]=='+') {
	if (!(u=get_user(word[2]))) {
		write_user(user,notloggedon);  return;
		}
	for (i=0;i<MAX_USER_CHANNEL;++i)
	        if (u->channel[i]==channel) break;
	if (i==MAX_USER_CHANNEL) {
		sprintf(text,"%s is not joined to this channel...\n",u->name);
		write_user(user,text);
		return;
		}

	inpstr=remove_first(remove_first(inpstr));
	if (user->vis) name=user->name; else name=invisname;
	substitute(text,channel->phraseto,name,u->name,channel->long_name);
	strcat(text,inpstr);
	strcat(text,"\n");
	write_to_listener_users(text,channel);
	record_shout(text,channel);
	return;
	}

if (word[1][0]=='-') {
	if (word[1][1]=='u') {
		for(i=0;i<MAX_USER_CHANNEL;++i) {
			if (user->channel[i]==channel) {
                		for (cnt=i;cnt<MAX_USER_CHANNEL-1;++cnt)
					user->channel[cnt]=user->channel[cnt+1];
				user->channel[MAX_USER_CHANNEL-1]=NULL;
				}
                	}
		if (user->vis) name=user->name; else name=invisname;
		substitute(text,channel->unjoin,name,"\0",channel->long_name);
		strcat(text,"\n");
		write_to_listener_users(text,channel);
		record_shout(text,channel);
		sprintf(text,"Disconnected from channel %s\n",channel->long_name);
		write_user(user,text);
		return;
		}

	if (word[1][1]=='r') {
		cnt=0;
		for(i=0;i<REVSHOUT_LINES;++i) {
		        line=(channel->revline+i)%REVSHOUT_LINES;
		        if (channel->revshout[line][0]) {
		                cnt++;
		                if (cnt==1) {
					sprintf(text,"\n~BB~FG*** Review buffer for the %s channel ***~RS\n\n",channel->long_name);
					write_user(user,text);
					}
	        	        write_user(user,channel->revshout[line]);
	        	        }
	        	}
		if (!cnt) write_user(user,"Review buffer is empty.\n");
		else write_user(user,"\n~BB~FG*** End ***~RS\n\n");
		return;
		}

	if (word[1][1]=='c') {
		for(i=0;i<REVSHOUT_LINES;++i) channel->revshout[i][0]='\0';
		channel->revline=0;
		sprintf(text,"[canale %s] %s has cleared the review buffer\n",channel->long_name,user->name);
		write_to_listener_users(text,channel);
		return;
		}
	}

if (user->vis) name=user->name; else name=invisname;
substitute(text,channel->phrase,name,"\0",channel->long_name);
strcat(text,inpstr);
strcat(text,"\n");
write_to_listener_users(text,channel);
record_shout(text,channel);
}


substitute(txt,pun2,var1,var2,var3)
char *txt,*pun2,*var1,*var2,*var3;
{
char *pun;
pun=txt;

while (*pun2!='\0') {
	if (*pun2=='%') {
		if (*(pun2+1)=='1') {
			*pun='\0';
			strcat(txt,var1);
			pun2+=2;
			pun+=strlen(var1);
			continue;
			}
		if (*(pun2+1)=='2') {
			*pun='\0';
			strcat(txt,var2);
			pun2+=2;
			pun+=strlen(var2);
			continue;
			}
		if (*(pun2+1)=='3') {
			*pun='\0';
			strcat(txt,var3);
			pun2+=2;
			pun+=strlen(var3);
			continue;
			}
		*pun='%';
		}
	else *pun=*pun2;
	pun++;
	pun2++;
	}

*pun='\0';

}
















/*** Tell another user something ***/
tell(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
char type[5],*name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot tell anyone anything.\n");  
	return;
	}
if (word_count<3) {
	write_user(user,"Tell who what?\n");  return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (u==user) {
	write_user(user,"Talking to yourself is the first sign of madness.\n");
	return;
	}
if (u->afk) {
	if (u->afk_mesg[0])
		sprintf(text,"%s is AFK, message is: %s\n",u->name,u->afk_mesg);
	else sprintf(text,"%s is AFK at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
if (u->ignall && (user->level<WIZ || u->level>user->level)) {
	if (u->malloc_start!=NULL) 
		sprintf(text,"%s is using the editor at the moment.\n",u->name);
	else sprintf(text,"%s is ignoring everyone at the moment.\n",u->name);
	write_user(user,text);  
	return;
	}
if (u->igntell && (user->level<WIZ || u->level>user->level)) {
	sprintf(text,"%s is ignoring tells at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
if (u->room==NULL) {
	sprintf(text,"%s is offsite and would not be able to reply to you.\n",u->name);
	write_user(user,text);
	return;
	}
inpstr=remove_first(inpstr);
if (inpstr[strlen(inpstr)-1]=='?') strcpy(type,"ask");
else strcpy(type,"tell");
sprintf(text,"~OLYou %s %s:~RS %s\n",type,u->name,inpstr);
write_user(user,text);
record_tell(user,text);
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~OL%s %ss you:~RS %s\n",name,type,inpstr);
write_user(u,text);
record_tell(u,text);
}


/*** Emote something ***/
emote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (word_count<2 && inpstr[1]<33) {
	write_user(user,"Emote what?\n");  return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
if (user->vis) name=user->name; else name=invisname;
if (inpstr[0]==';') sprintf(text,"%s%s\n",name,inpstr+1);
else sprintf(text,"%s %s\n",name,inpstr);
write_room(user->room,text);
record(user->room,text);
}


/*** Do a private emote ***/
pemote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;
UR_OBJECT u;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (word_count<3) {
	write_user(user,"Private emote what?\n");  return;
	}
word[1][0]=toupper(word[1][0]);
if (!strcmp(word[1],user->name)) {
	write_user(user,"Emoting to yourself is the second sign of madness.\n");
	return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (u->afk) {
	if (u->afk_mesg[0])
		sprintf(text,"%s is AFK, message is: %s\n",u->name,u->afk_mesg);
	else sprintf(text,"%s is AFK at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
if (u->ignall && (user->level<WIZ || u->level>user->level)) {
	if (u->malloc_start!=NULL) 
		sprintf(text,"%s is using the editor at the moment.\n",u->name);
	else sprintf(text,"%s is ignoring everyone at the moment.\n",u->name);
	write_user(user,text);  return;
	}
if (u->igntell && (user->level<WIZ || u->level>user->level)) {
	sprintf(text,"%s is ignoring private emotes at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
if (u->room==NULL) {
	sprintf(text,"%s is offsite and would not be able to reply to you.\n",u->name);
	write_user(user,text);
	return;
	}
if (user->vis) name=user->name; else name=invisname;
inpstr=remove_first(inpstr);
sprintf(text,"~OL(To %s)~RS %s %s\n",u->name,name,inpstr);
write_user(user,text);
sprintf(text,"~OL>>~RS %s %s\n",name,inpstr);
write_user(u,text);
record_tell(u,text);
}


/*** Echo something to screen ***/
echo(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot echo.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Echo what?\n");  return;
	}
sprintf(text,"(%s) ",user->name);
write_level(WIZ,1,text,NULL);
sprintf(text,"- %s\n",inpstr);
write_room(user->room,text);
record(user->room,text);
}



/*** Move to another room ***/
go(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;
NL_OBJECT nl;
int i;
char tvar[ROOM_NAME_LEN+1];


if (word_count<2) {
	write_user(user,"Go where?\n");  return;
	}
nl=user->room->netlink;


if (nl!=NULL && !strncmp(nl->service,word[1],strlen(word[1]))) {
	if (user->pot_netlink==nl) {
		write_user(user,"The remote service may be lagged, please be patient...\n");
		return;
		}
	rm=user->room;
	if (nl->stage<2) {
		write_user(user,"The netlink is inactive.\n");
		return;
		}
	if (nl->allow==IN && user->netlink!=nl) {
		/* Link for incoming users only */
		write_user(user,"Sorry, link is for incoming users only.\n");
		return;
		}
	/* If site is users home site then tell home system that we have removed
	   him. */
	if (user->netlink==nl) {
		write_user(user,"~FB~OLYou traverse cyberspace...\n");
		sprintf(text,"REMVD %s\n",user->name);
		write_sock(nl->socket,text);
		if (user->vis) {
			sprintf(text,"%s goes to the %s\n",user->name,nl->service);
			write_room_except(rm,text,user);
			}
		else write_room_except(rm,invisleave,user);
		sprintf(text,"NETLINK: Remote user %s removed.\n",user->name);
		write_syslog(text,1,TOSYS);
		destroy_user_clones(user);
		destruct_user(user);
		reset_access(rm);
		num_of_users--;
		no_prompt=1;
		return;
		}
	/* Can't let remote user jump to yet another remote site because this will 
	   reset his user->netlink value and so we will lose his original link.
	   2 netlink pointers are needed in the user structure to allow this
	   but it means way too much rehacking of the code and I don't have the 
	   time or inclination to do it */
	if (user->type==REMOTE_TYPE) {
		write_user(user,"Sorry, due to software limitations you can only traverse one netlink.\n");
		return;
		}
	if (nl->ver_major<=3 && nl->ver_minor<=3 && nl->ver_patch<1) {
		if (!word[2][0]) 
			sprintf(text,"TRANS %s %s %s\n",user->name,user->pass,user->desc);
		else sprintf(text,"TRANS %s %s %s\n",user->name,(char *)crypt(word[2],"NU"),user->desc);
		}
	else {
		if (!word[2][0]) 
			sprintf(text,"TRANS %s %s %d %s\n",user->name,user->pass,user->level,user->desc);
		else sprintf(text,"TRANS %s %s %d %s\n",user->name,(char *)crypt(word[2],"NU"),user->level,user->desc);
		}	
	write_sock(nl->socket,text);
	user->remote_com=GO;
	user->pot_netlink=nl;  /* potential netlink */
	no_prompt=1;
	return;
	}
/* If someone tries to go somewhere else while waiting to go to a talker
   send the other site a release message */
if (user->remote_com==GO) {
	sprintf(text,"REL %s\n",user->name);
	write_sock(user->pot_netlink->socket,text);
	user->remote_com=-1;
	user->pot_netlink=NULL;
	}


get_end(inpstr);
inump(inpstr);


if ((rm=get_room(inpstr,user))==NULL) return;

strcpy(tvar,rm->name);
delump(tvar);

if (rm==user->room) {
	sprintf(text,"You are already in the %s!\n",tvar);
	write_user(user,text);
	return;
	}

/* See if link from current room */
for(i=0;i<MAX_LINKS;++i) {
	if (user->room->link[i]==rm) {
		move_user(user,rm,0);  return;
		}
	}
if (user->level<USER) {
	sprintf(text,"The %s is not adjoined to here.\n",tvar);
	write_user(user,text);  
	return;
	}
move_user(user,rm,1);
}


/*** Called by go() and move() ***/
move_user(user,rm,teleport)
UR_OBJECT user;
RM_OBJECT rm;
int teleport;
{
RM_OBJECT old_room;
char tvar[ROOM_NAME_LEN+1];

strcpy(tvar,rm->name);
delump(tvar);

old_room=user->room;
if (teleport!=2 && !has_room_access(user,rm)) {
	write_user(user,"That room is currently private, you cannot enter.\n");  
	return;
	}
/* Reset invite room if in it */
if (user->invite_room==rm) user->invite_room=NULL;
if (!user->vis) {
	write_room(rm,invisenter);
	write_room_except(user->room,invisleave,user);
	goto SKIP;
	}
if (teleport==1) {
	sprintf(text,"~FT~OL%s appears in an explosion of blue magic!\n",user->name);
	write_room(rm,text);
	sprintf(text,"~FT~OL%s chants a spell and vanishes into a magical blue vortex!\n",user->name);
	write_room_except(old_room,text,user);
	goto SKIP;
	}
if (teleport==2) {
	write_user(user,"\n~FT~OLA giant hand grabs you and pulls you into a magical blue vortex!\n");
	sprintf(text,"~FT~OL%s falls out of a magical blue vortex!\n",user->name);
	write_room(rm,text);
	if (old_room==NULL) {
		sprintf(text,"REL %s\n",user->name);
		write_sock(user->netlink->socket,text);
		user->netlink=NULL;
		}
	else {
		sprintf(text,"~FT~OLA giant hand grabs %s who is pulled into a magical blue vortex!\n",user->name);
		write_room_except(old_room,text,user);
		}
	goto SKIP;
	}
sprintf(text,"%s %s.\n",user->name,user->in_phrase);
write_room(rm,text);
sprintf(text,"%s %s to the %s.\n",user->name,user->out_phrase,tvar);
write_room_except(user->room,text,user);

SKIP:
user->room=rm;
look(user,0);
reset_access(old_room);
}


/*** Switch ignoring all on and off ***/
toggle_ignall(user)
UR_OBJECT user;
{
if (!user->ignall) {
	write_user(user,"You are now ignoring everyone.\n");
	sprintf(text,"%s is now ignoring everyone.\n",user->name);
	write_room_except(user->room,text,user);
	user->ignall=1;
	return;
	}
write_user(user,"You will now hear everyone again.\n");
sprintf(text,"%s is listening again.\n",user->name);
write_room_except(user->room,text,user);
user->ignall=0;
}


/*** Switch prompt on and off ***/
toggle_prompt(user)
UR_OBJECT user;
{
if (user->prompt) {
	write_user(user,"Prompt ~FROFF.\n");
	user->prompt=0;  return;
	}
write_user(user,"Prompt ~FGON.\n");
user->prompt=1;
}


/*** Set user description ***/
set_desc(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (word_count<2) {
	sprintf(text,"Your current description is: %s\n",user->desc);
	write_user(user,text);
	return;
	}
if (strstr(word[1],"(CLONE)")) {
	write_user(user,"You cannot have that description.\n");  return;
	}
if (strlen(inpstr)>USER_DESC_LEN) {
	write_user(user,"Description too long.\n");  return;
	}
strcpy(user->desc,inpstr);
write_user(user,"Description set.\n");
}


/*** Set in and out phrases ***/
set_iophrase(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (strlen(inpstr)>PHRASE_LEN) {
	write_user(user,"Phrase too long.\n");  return;
	}
if (com_num==INPHRASE) {
	if (word_count<2) {
		sprintf(text,"Your current in phrase is: %s\n",user->in_phrase);
		write_user(user,text);
		return;
		}
	strcpy(user->in_phrase,inpstr);
	write_user(user,"In phrase set.\n");
	return;
	}
if (word_count<2) {
	sprintf(text,"Your current out phrase is: %s\n",user->out_phrase);
	write_user(user,text);
	return;
	}
strcpy(user->out_phrase,inpstr);
write_user(user,"Out phrase set.\n");
}


/*** Set rooms to public or private ***/
set_room_access(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;
int cnt;

inump(inpstr);

rm=user->room;
if (word_count<2) rm=user->room;
else {
	if (user->level<=gatecrash_level) {
		write_user(user,"You are not a high enough level to use the room option.\n");  
		return;
		}
	if ((rm=get_room(inpstr,user))==NULL) return;
	}
if (user->vis) name=user->name; else name=invisname;
if (rm->access>PRIVATE) {
	if (rm==user->room) 
		write_user(user,"This room's access is fixed.\n"); 
	else write_user(user,"That room's access is fixed.\n");
	return;
	}
if (com_num==PUBCOM && rm->access==PUBLIC) {
	if (rm==user->room) 
		write_user(user,"This room is already public.\n");  
	else write_user(user,"That room is already public.\n"); 
	return;
	}
if (user->vis) name=user->name; else name=invisname;
if (com_num==PRIVCOM) {
	if (rm->access==PRIVATE) {
		if (rm==user->room) 
			write_user(user,"This room is already private.\n");  
		else write_user(user,"That room is already private.\n"); 
		return;
		}
	cnt=0;
	for(u=user_first;u!=NULL;u=u->next) if (u->room==rm) ++cnt;
	if (cnt<min_private_users && user->level<ignore_mp_level) {
		sprintf(text,"You need at least %d users/clones in a room before it can be made private.\n",min_private_users);
		write_user(user,text);
		return;
		}
	write_user(user,"Room set to ~FRPRIVATE.\n");
	if (rm==user->room) {
		sprintf(text,"%s has set the room to ~FRPRIVATE.\n",name);
		write_room_except(rm,text,user);
		}
	else write_room(rm,"This room has been set to ~FRPRIVATE.\n");
	rm->access=PRIVATE;
	return;
	}
write_user(user,"Room set to ~FGPUBLIC.\n");
if (rm==user->room) {
	sprintf(text,"%s has set the room to ~FGPUBLIC.\n",name);
	write_room_except(rm,text,user);
	}
else write_room(rm,"This room has been set to ~FGPUBLIC.\n");
rm->access=PUBLIC;

/* Reset any invites into the room & clear review buffer */
for(u=user_first;u!=NULL;u=u->next) {
	if (u->invite_room==rm) u->invite_room=NULL;
	}
clear_revbuff(rm);
}


/*** Ask to be let into a private room ***/
letmein(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;
int i;
char tvar[ROOM_NAME_LEN+1];

inump(inpstr);


if (word_count<2) {
	write_user(user,"Let you into where?\n");  return;
	}
if ((rm=get_room(inpstr,user))==NULL) return;
strcpy(tvar,rm->name);
delump(tvar);

if (rm==user->room) {
	sprintf(text,"You are already in the %s!\n",tvar);
	write_user(user,text);
	return;
	}
for(i=0;i<MAX_LINKS;++i) 
	if (user->room->link[i]==rm) goto GOT_IT;
sprintf(text,"The %s is not adjoined to here.\n",tvar);
write_user(user,text);  
return;

GOT_IT:
if (!(rm->access & PRIVATE)) {
	sprintf(text,"The %s is currently public.\n",tvar);
	write_user(user,text);
	return;
	}
sprintf(text,"You shout asking to be let into the %s.\n",tvar);
write_user(user,text);
sprintf(text,"%s shouts asking to be let into the %s.\n",user->name,tvar);
write_room_except(user->room,text,user);
sprintf(text,"%s shouts asking to be let in.\n",user->name);
write_room(rm,text);
}


/*** Invite a user into a private room ***/
invite(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;
char tvar[ROOM_NAME_LEN+1];

if (word_count<2) {
	write_user(user,"Invite who?\n");  return;
	}
rm=user->room;
if (!(rm->access & PRIVATE)) {
	write_user(user,"This room is currently public.\n");
	return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (u==user) {
	write_user(user,"Inviting yourself to somewhere is the third sign of madness.\n");
	return;
	}
if (u->room==rm) {
	sprintf(text,"%s is already here!\n",u->name);
	write_user(user,text);
	return;
	}
if (u->invite_room==rm) {
	sprintf(text,"%s has already been invited into here.\n",u->name);
	write_user(user,text);
	return;
	}
sprintf(text,"You invite %s in.\n",u->name);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
strcpy(tvar,rm->name);
delump(tvar);
sprintf(text,"%s has invited you into the %s.\n",name,tvar);
write_user(u,text);
u->invite_room=rm;
}


/*** Set the room topic ***/
set_topic(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;
char *name;

rm=user->room;
if (word_count<2) {
	if (!strlen(rm->topic)) {
		write_user(user,"No topic has been set yet.\n");  return;
		}
	sprintf(text,"The current topic is: %s\n",rm->topic);
	write_user(user,text);
	return;
	}
if (strlen(inpstr)>TOPIC_LEN) {
	write_user(user,"Topic too long.\n");  return;
	}
sprintf(text,"Topic set to: %s\n",inpstr);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s has set the topic to: %s\n",name,inpstr);
write_room_except(rm,text,user);
strcpy(rm->topic,inpstr);
}


/*** Wizard moves a user to another room ***/
move(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;
char tvar[ROOM_NAME_LEN+1];

if (word_count<2) {
	write_user(user,"Usage: move <user> [<room>]\n");  return;
	}

inpstr=remove_first(inpstr);
inump(inpstr);


if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (word_count<3) rm=user->room;
else {
	if ((rm=get_room(inpstr,user))==NULL) return;
	}

strcpy(tvar,rm->name);
delump(tvar);


if (user==u) {
	write_user(user,"Trying to move yourself this way is the fourth sign of madness.\n");  return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot move a user of equal or higher level than yourself.\n");
	return;
	}
if (rm==u->room) {
	sprintf(text,"%s is already in the %s.\n",u->name,tvar);
	write_user(user,text);
	return;
	};
if (!has_room_access(user,rm)) {
	sprintf(text,"The %s is currently private, %s cannot be moved there.\n",tvar,u->name);
	write_user(user,text);  
	return;
	}
write_user(user,"~FT~OLYou chant an ancient spell...\n");
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~FT~OL%s chants an ancient spell...\n",name);
write_room_except(user->room,text,user);
move_user(u,rm,2);
prompt(u);
}


/*** Broadcast an important message ***/
bcast(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (word_count<2) {
	write_user(user,"Usage: bcast <message>\n");  return;
	}
if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot broadcast anything.\n");  
	return;
	}
force_listen=1;
if (user->vis) 
	sprintf(text,"\07\n~BR*** Broadcast message from %s ***\n%s\n\n",user->name,inpstr);
else sprintf(text,"\07\n~BR*** Broadcast message ***\n%s\n\n",inpstr);
write_room(NULL,text);  
}


/*** Show who is on ***/
who(user,people)
UR_OBJECT user;
int people;
{
UR_OBJECT u;
int cnt,total,invis,mins,remote,idle,logins;
char line[USER_NAME_LEN+USER_DESC_LEN*2];
char rname[ROOM_NAME_LEN+1],portstr[5],idlestr[6],sockstr[3];

total=0;  invis=0;  remote=0;  logins=0;
if (user->login) sprintf(text,"\n    *** Current users %s ***\n",long_date(1));
else sprintf(text,"\n    ~BB*** Current users %s ***~RS\n",long_date(1));
write_user(user,text);
if (!people) {
write_user(user,"+------------------------------------------+------------------------------+\n");
write_user(user,"|   Name                                   +  Where is                    |\n");
write_user(user,"+------------------------------------------+------------------------------+\n");
}

if (people) write_user(user,"~FTName            : Level Line Ignall Visi Idle Mins  Port  Site/Service\n\n\r");
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE) continue;
	mins=(int)(time(0) - u->last_login)/60;
	idle=(int)(time(0) - u->last_input)/60;
	if (u->type==REMOTE_TYPE) strcpy(portstr,"   -");
	else {
		if (u->port==port[0]) strcpy(portstr,"MAIN");
		else strcpy(portstr," WIZ");
		}
	if (u->login) {
		if (!people) continue;
		sprintf(text,"~FY[Login stage %d] :     -   %2d      -    - %4d    -  %s  %s:%d\n",5 - u->login,u->socket,idle,portstr,u->site,u->site_port);
		write_user(user,text);
		logins++;
		continue;
		}
	++total;
	if (u->type==REMOTE_TYPE) ++remote;
	if (!u->vis) { 
		++invis;  
		if (u->level>user->level) continue;  
		}
	if (people) {
		if (u->afk) strcpy(idlestr," AFK");
		else sprintf(idlestr,"%4d",idle);
		if (u->type==REMOTE_TYPE) strcpy(sockstr," -");
		else sprintf(sockstr,"%2d",u->socket);
		sprintf(text,"%-15s :  %4s   %s    %s  %s %s %4d  %s %s\n",u->name,level_name[u->path][u->level],sockstr,noyes1[u->ignall],noyes1[u->vis],idlestr,mins,portstr,u->site);
		write_user(user,text);
		continue;
		}
	sprintf(line,"  %s %s~RS",u->name,u->desc);
	if (!u->vis) line[0]='*';
	if (u->type==REMOTE_TYPE) line[1]='@';
	if (u->room==NULL) sprintf(rname,"@%s",u->netlink->service);
	else strcpy(rname,u->room->name);
	delump(rname);
	/* Count number of colour coms to be taken account of when formatting */
	cnt=colour_com_count(line);
	sprintf(text,"| %-*s | %-28s |",40+cnt*3,line,rname);
	if (u->afk) text[1]='!';
	strcat(text,"\n");
	write_user(user,text);
	}
if (!people) write_user(user,"+------------------------------------------+------------------------------+\n");
sprintf(text,"\nThere are %d visible, %d invisible, %d remote users.\nTotal of %d users",num_of_users-invis,invis,remote,total);
if (people) sprintf(text,"%s and %d logins.\n\n",text,logins);
else strcat(text,".\n\n");
write_user(user,text);
}


/*** Do the help ***/
help(user)
UR_OBJECT user;
{
int ret;
char filename[80];
char *c;

if (word_count<2) {
	help_commands(user);
	return;
	}

if (!strcmp(word[1],"credits")) {  help_credits(user);  return;  }

/* Check for any illegal crap in searched for filename so they cannot list 
   out the /etc/passwd file for instance. */
c=word[1];
while(*c) {
	if (*c=='.' || *c++=='/') {
		write_user(user,"Sorry, there is no help on that topic.\n");
		return;
		}
	}
sprintf(filename,"%s/%s",HELPFILES,word[1]);
if (!(ret=more(user,user->socket,filename)))
	write_user(user,"Sorry, there is no help on that topic.\n");
if (ret==1) user->misc_op=2;
}


/*** Show the command available ***/
help_commands(user)
UR_OBJECT user;
{
int com,cnt,lev;
char temp[20];

sprintf(text,"\n~BB*** Commands available for level: %s ***~RS\n\n",level_name[user->path][user->level]);
write_user(user,text);

for(lev=NEW;lev<=user->level;++lev) {
	sprintf(text,"~FT(%s)\n",level_name[user->path][lev]);
	write_user(user,text);
	com=0;  cnt=0;  text[0]='\0';
	while(command[com][0]!='*') {
		if (com_level[com]!=lev) {  com++;  continue;  }
		sprintf(temp,"%-10s ",command[com]);
		strcat(text,temp);
		if (cnt==6) {  
			strcat(text,"\n");  
			write_user(user,text);  
			text[0]='\0';  cnt=-1;  
			}
		com++; cnt++;
		}
	if (cnt) {
		strcat(text,"\n");  write_user(user,text);
		}
	}
write_user(user,"\nType '~FG.help <command name>~RS' for specific help on a command.\nRemember, you can use a '.' on its own to repeat your last command or speech.\n\n");
}


/*** Show the credits. Add your own credits here if you wish but PLEASE leave 
     my credits intact. Thanks. */
help_credits(user)
UR_OBJECT user;
{
sprintf(text,"\n~BB*** The Credits :) ***\n\n~BRNUTS version %s, Copyright (C) Neil Robertson 1996.\n\n",VERSION);
write_user(user,text);
write_user(user,"~BM             ~BB             ~BT             ~BG             ~BY             ~BR             ~RS\n");
write_user(user,"NUTS stands for Neils Unix Talk Server, a program which started out as a\nuniversity project in autumn 1992 and has progressed from thereon. In no\nparticular order thanks go to the following people who helped me develop or\n");
write_user(user,"debug this code in one way or another over the years:\n   ~FTDarren Seryck, Steve Guest, Dave Temple, Satish Bedi, Tim Bernhardt,\n   ~FTKien Tran, Jesse Walton, Pak Chan, Scott MacKenzie and Bryan McPhail.\n"); 
write_user(user,"Also thanks must go to anyone else who has emailed me with ideas and/or bug\nreports and all the people who have used NUTS over the intervening years.\n");
write_user(user,"I know I've said this before but this time I really mean it - this is the final\nversion of NUTS 3. In a few years NUTS 4 may spring forth but in the meantime\nthat, as they say, is that. :)\n\n");
write_user(user,"If you wish to email me my address is '~FGneil@ogham.demon.co.uk~RS' and should\nremain so for the forseeable future.\n\nNeil Robertson - November 1996.\n");
write_user(user,"~BM             ~BB             ~BT             ~BG             ~BY             ~BR             ~RS\n\n");
}


/*** Read the message board ***/
read_board(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;
char filename[80],*name;
int ret;
char tvar[ROOM_NAME_LEN+1];

if (word_count<2) rm=user->room;
else {
	inump(inpstr);
	if ((rm=get_room(inpstr,user))==NULL) return;
	if (!has_room_access(user,rm)) {
		write_user(user,"That room is currently private, you cannot read the board.\n");
		return;
		}
	}

strcpy(tvar,rm->name);
delump(tvar);

	
sprintf(text,"\n~BB*** The %s message board ***~RS\n\n",tvar);
write_user(user,text);
sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
if (!(ret=more(user,user->socket,filename))) 
	write_user(user,"There are no messages on the board.\n\n");
else if (ret==1) user->misc_op=2;
if (user->vis) name=user->name; else name=invisname;
if (rm==user->room) {
	sprintf(text,"%s reads the message board.\n",name);
	write_room_except(user->room,text,user);
	}
}


/*** Write on the message board ***/
write_board(user,inpstr,done_editing)
UR_OBJECT user;
char *inpstr;
int done_editing;
{
FILE *fp;
int cnt,inp;
char *ptr,*name,filename[80];

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot write on the board.\n");  
	return;
	}
if (!done_editing) {
	if (word_count<2) {
		if (user->type==REMOTE_TYPE) {
			/* Editor won't work over netlink cos all the prompts will go
			   wrong, I'll address this in a later version. */
			write_user(user,"Sorry, due to software limitations remote users cannot use the line editor.\nUse the '.write <mesg>' method instead.\n");
			return;
			}
		write_user(user,"\n~BB*** Writing board message ***~RS\n\n");
		user->misc_op=3;
		editor(user,NULL,MAX_LINES);
		return;
		}
	ptr=inpstr;
	inp=1;
	}
else {
	ptr=user->malloc_start;  inp=0;
	}

sprintf(filename,"%s/%s.B",DATAFILES,user->room->name);
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: cannot write to file.\n",syserror);
	write_user(user,text);
	sprintf(text,"ERROR: Couldn't open file %s to append in write_board().\n",filename);
	write_syslog(text,0,TOSYS);
	return;
	}
if (user->vis) name=user->name; else name=invisname;
/* The posting time (PT) is the time its written in machine readable form, this 
   makes it easy for this program to check the age of each message and delete 
   as appropriate in check_messages() */
if (user->type==REMOTE_TYPE) 
	sprintf(text,"PT: %d\r~OLFrom: %s@%s  %s\n",(int)(time(0)),name,user->netlink->service,long_date(0));
else sprintf(text,"PT: %d\r~OLFrom: %s  %s\n",(int)(time(0)),name,long_date(0));
fputs(text,fp);
cnt=0;
while(*ptr!='\0') {
	putc(*ptr,fp);
	if (*ptr=='\n') cnt=0; else ++cnt;
	if (cnt==80) { putc('\n',fp); cnt=0; }
	++ptr;
	}
if (inp) fputs("\n\n",fp); else putc('\n',fp);
fclose(fp);
write_user(user,"You write the message on the board.\n");
sprintf(text,"%s writes a message on the board.\n",name);
write_room_except(user->room,text,user);
user->room->mesg_cnt++;
}



/*** Wipe some messages off the board ***/
wipe_board(user)
UR_OBJECT user;
{
int num,cnt,valid;
char infile[80],line[82],id[82],*name;
FILE *infp,*outfp;
RM_OBJECT rm;

if (word_count<2 || ((num=atoi(word[1]))<1 && strcmp(word[1],"all"))) {
	write_user(user,"Usage: wipe <number of messages>/all\n");  return;
	}
rm=user->room;
if (user->vis) name=user->name; else name=invisname;
sprintf(infile,"%s/%s.B",DATAFILES,rm->name);
if (!(infp=fopen(infile,"r"))) {
	write_user(user,"The message board is empty.\n");
	return;
	}
if (!strcmp(word[1],"all")) {
	fclose(infp);
	unlink(infile);
	write_user(user,"All messages deleted.\n");
	sprintf(text,"%s wipes the message board.\n",name);
	write_room_except(rm,text,user);
	sprintf(text,"%s wiped all messages from the board in the %s.\n",user->name,rm->name);
	write_syslog(text,1,TOSYS);
	rm->mesg_cnt=0;
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile in wipe_board().\n",0,TOSYS);
	fclose(infp);
	return;
	}
cnt=0; valid=1;
fgets(line,82,infp); /* max of 80+newline+terminator = 82 */
while(!feof(infp)) {
	if (cnt<=num) {
		if (*line=='\n') valid=1;
		sscanf(line,"%s",id);
		if (valid && !strcmp(id,"PT:")) {
			if (++cnt>num) fputs(line,outfp);
			valid=0;
			}
		}
	else fputs(line,outfp);
	fgets(line,82,infp);
	}
fclose(infp);
fclose(outfp);
unlink(infile);
if (cnt<num) {
	unlink("tempfile");
	sprintf(text,"There were only %d messages on the board, all now deleted.\n",cnt);
	write_user(user,text);
	sprintf(text,"%s wipes the message board.\n",name);
	write_room_except(rm,text,user);
	sprintf(text,"%s wiped all messages from the board in the %s.\n",user->name,rm->name);
	write_syslog(text,1,TOSYS);
	rm->mesg_cnt=0;
	return;
	}
if (cnt==num) {
	unlink("tempfile"); /* cos it'll be empty anyway */
	write_user(user,"All messages deleted.\n");
	user->room->mesg_cnt=0;
	sprintf(text,"%s wiped all messages from the board in the %s.\n",user->name,rm->name);
	}
else {
	rename("tempfile",infile);
	sprintf(text,"%d messages deleted.\n",num);
	write_user(user,text);
	user->room->mesg_cnt-=num;
	sprintf(text,"%s wiped %d messages from the board in the %s.\n",user->name,num,rm->name);
	}
write_syslog(text,1,TOSYS);
sprintf(text,"%s wipes the message board.\n",name);
write_room_except(rm,text,user);
}

	

/*** Search all the boards for the words given in the list. Rooms fixed to
	private will be ignore if the users level is less than gatecrash_level ***/
search_boards(user)
UR_OBJECT user;
{
RM_OBJECT rm;
FILE *fp;
char filename[80],line[82],buff[(MAX_LINES+1)*82],w1[81];
int w,cnt,message,yes,room_given;

if (word_count<2) {
	write_user(user,"Usage: search <word list>\n");  return;
	}
/* Go through rooms */
cnt=0;
for(rm=room_first;rm!=NULL;rm=rm->next) {
	sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
	if (!(fp=fopen(filename,"r"))) continue;
	if (!has_room_access(user,rm)) {  fclose(fp);  continue;  }

	/* Go through file */
	fgets(line,81,fp);
	yes=0;  message=0;  
	room_given=0;  buff[0]='\0';
	while(!feof(fp)) {
		if (*line=='\n') {
			if (yes) {  strcat(buff,"\n");  write_user(user,buff);  }
			message=0;  yes=0;  buff[0]='\0';
			}
		if (!message) {
			w1[0]='\0';  
			sscanf(line,"%s",w1);
			if (!strcmp(w1,"PT:")) {  
				message=1;  
				strcpy(buff,remove_first(remove_first(line)));
				}
			}
		else strcat(buff,line);
		for(w=1;w<word_count;++w) {
			if (!yes && strstr(line,word[w])) {  
				if (!room_given) {
					sprintf(text,"~BB*** %s ***~RS\n\n",rm->name);
					write_user(user,text);
					room_given=1;
					}
				yes=1;  cnt++;  
				}
			}
		fgets(line,81,fp);
		}
	if (yes) {  strcat(buff,"\n");  write_user(user,buff);  }
	fclose(fp);
	}
if (cnt) {
	sprintf(text,"Total of %d matching messages.\n\n",cnt);
	write_user(user,text);
	}
else write_user(user,"No occurences found.\n");
}



/*** See review of conversation ***/
review(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm=user->room;
int i,line,cnt;
char tvar[ROOM_NAME_LEN+1];


if (word_count<2) rm=user->room;
else {
	inump(inpstr);
	if ((rm=get_room(inpstr,user))==NULL) return;
	if (!has_room_access(user,rm)) {
		write_user(user,"That room is currently private, you cannot review the conversation.\n");
		return;
		}
	}

strcpy(tvar,rm->name);
delump(tvar);


cnt=0;
for(i=0;i<REVIEW_LINES;++i) {
	line=(rm->revline+i)%REVIEW_LINES;
	if (rm->revbuff[line][0]) {
		cnt++;
		if (cnt==1) {
			sprintf(text,"\n~BB~FG*** Review buffer for the %s ***~RS\n\n",tvar);
			write_user(user,text);
			}
		write_user(user,rm->revbuff[line]); 
		}
	}
if (!cnt) write_user(user,"Review buffer is empty.\n");
else write_user(user,"\n~BB~FG*** End ***~RS\n\n");
}


/*** Return to home site ***/
home(user)
UR_OBJECT user;
{
if (user->room!=NULL) {
	write_user(user,"You are already on your home system.\n");
	return;
	}
write_user(user,"~FB~OLYou traverse cyberspace...\n");
sprintf(text,"REL %s\n",user->name);
write_sock(user->netlink->socket,text);
sprintf(text,"NETLINK: %s returned from %s.\n",user->name,user->netlink->service);
write_syslog(text,1,TOSYS);
user->room=user->netlink->connect_room;
user->netlink=NULL;
if (user->vis) {
	sprintf(text,"%s %s\n",user->name,user->in_phrase);
	write_room_except(user->room,text,user);
	}
else write_room_except(user->room,invisenter,user);
look(user,0);
}


/*** Show some user stats ***/
status(user)
UR_OBJECT user;
{
UR_OBJECT u;
char ir[ROOM_NAME_LEN+1];
int days,hours,mins,hs;

if (word_count<2 || user->level<WIZ) {
	u=user;
	write_user(user,"\n~BB*** Your status ***~RS\n\n");
	}
else {
	if (!(u=get_user(word[1]))) {
		write_user(user,notloggedon);  return;
		}
	if (u->level>user->level) {
		write_user(user,"You cannot stat a user of a higher level than yourself.\n");
		return;
		}
	sprintf(text,"\n~BB*** %s's status ***~RS\n\n",u->name);
	write_user(user,text);
	}
if (u->invite_room==NULL) strcpy(ir,"<nowhere>");
else strcpy(ir,u->invite_room->name);
sprintf(text,"Level       : %s\nIgnoring all: %s\n",level_name[u->path][u->level],noyes2[u->ignall]);
write_user(user,text);
sprintf(text,"Ign. tells  : %s\n",noyes2[u->igntell]);
write_user(user,text);
if (u->type==REMOTE_TYPE || u->room==NULL) hs=0; else hs=1;
sprintf(text,"On home site: %s\nVisible     : %s\n",noyes2[hs],noyes2[u->vis]);
write_user(user,text);
sprintf(text,"Muzzled     : %s\nUnread mail : %s\n",noyes2[(u->muzzled>0)],noyes2[has_unread_mail(u)]);
write_user(user,text);
sprintf(text,"Char echo   : %s\nColour      : %s\nInvited to  : %s\n",offon[u->charmode_echo],offon[u->colour],ir);
write_user(user,text);
sprintf(text,"Description : %s\nIn phrase   : %s\nOut phrase  : %s\n",u->desc,u->in_phrase,u->out_phrase);
write_user(user,text);
mins=(int)(time(0) - u->last_login)/60;
sprintf(text,"Online for  : %d minutes\n",mins);
days=u->total_login/86400;
hours=(u->total_login%86400)/3600;
mins=(u->total_login%3600)/60;
sprintf(text,"Total login : %d days, %d hours, %d minutes\n\n",days,hours,mins);
write_user(user,text);
}



/*** Read your mail ***/
rmail(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
int ret;
char c,filename[80],line[DNL+1];

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"You have no mail.\n");  return;
	}
/* Update last read / new mail received time at head of file */
if (outfp=fopen("tempfile","w")) {
	fprintf(outfp,"%d\r",(int)(time(0)));
	/* skip first line of mail file */
	fgets(line,DNL,infp);

	/* Copy rest of file */
	c=getc(infp);
	while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }

	fclose(outfp);
	rename("tempfile",filename);
	}
user->read_mail=time(0);
fclose(infp);
write_user(user,"\n~BB*** Your mail ***~RS\n\n");
ret=more(user,user->socket,filename);
if (ret==1) user->misc_op=2;
}



/*** Send mail message ***/
smail(user,inpstr,done_editing)
UR_OBJECT user;
char *inpstr;
int done_editing;
{
UR_OBJECT u;
FILE *fp;
int remote,has_account;
char *c,filename[80];

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot mail anyone.\n");  return;
	}
if (done_editing) {
	send_mail(user,user->mail_to,user->malloc_start);
	user->mail_to[0]='\0';
	return;
	}
if (word_count<2) {
	write_user(user,"Smail who?\n");  return;
	}
/* See if its to another site */
remote=0;
has_account=0;
c=word[1];
while(*c) {
	if (*c=='@') {  
		if (c==word[1]) {
			write_user(user,"Users name missing before @ sign.\n");  
			return;
			}
		remote=1;  break;  
		}
	++c;
	}
word[1][0]=toupper(word[1][0]);
/* See if user exists */
if (!remote) {
	u=NULL;
	if (!(u=get_user(word[1]))) {
		sprintf(filename,"%s/%s.D",USERFILES,word[1]);
		if (!(fp=fopen(filename,"r"))) {
			write_user(user,nosuchuser);  return;
			}
		has_account=1;
		fclose(fp);
		}
	if (u==user) {
		write_user(user,"Trying to mail yourself is the fifth sign of madness.\n");
		return;
		}
	if (u!=NULL) strcpy(word[1],u->name); 
	if (!has_account) {
		/* See if user has local account */
		sprintf(filename,"%s/%s.D",USERFILES,word[1]);
		if (!(fp=fopen(filename,"r"))) {
			sprintf(text,"%s is a remote user and does not have a local account.\n",u->name);
			write_user(user,text);  
			return;
			}
		fclose(fp);
		}
	}
if (word_count>2) {
	/* One line mail */
	strcat(inpstr,"\n"); 
	send_mail(user,word[1],remove_first(inpstr));
	return;
	}
if (user->type==REMOTE_TYPE) {
	write_user(user,"Sorry, due to software limitations remote users cannot use the line editor.\nUse the '.smail <user> <mesg>' method instead.\n");
	return;
	}
sprintf(text,"\n~BB*** Writing mail message to %s ***~RS\n\n",word[1]);
write_user(user,text);
user->misc_op=4;
strcpy(user->mail_to,word[1]);
editor(user,NULL,MAX_LINES);
}


/*** Delete some or all of your mail. A problem here is once we have deleted
     some mail from the file do we mark the file as read? If not we could
     have a situation where the user deletes all his mail but still gets
     the YOU HAVE UNREAD MAIL message on logging on if the idiot forgot to 
     read it first. ***/
dmail(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
int num,valid,cnt;
char filename[80],w1[ARR_SIZE],line[ARR_SIZE];

if (word_count<2 || ((num=atoi(word[1]))<1 && strcmp(word[1],"all"))) {
	write_user(user,"Usage: dmail <number of messages>/all\n");  return;
	}
sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"You have no mail to delete.\n");  return;
	}
if (!strcmp(word[1],"all")) {
	fclose(infp);
	unlink(filename);
	write_user(user,"All mail deleted.\n");
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile in dmail().\n",0,TOSYS);
	fclose(infp);
	return;
	}
fprintf(outfp,"%d\r",(int)time(0));
user->read_mail=time(0);
cnt=0;  valid=1;
fgets(line,DNL,infp); /* Get header date */
fgets(line,ARR_SIZE-1,infp);
while(!feof(infp)) {
	if (cnt<=num) {
		if (*line=='\n') valid=1;
		sscanf(line,"%s",w1);
		if (valid && (!strcmp(w1,"~OLFrom:") || !strcmp(w1,"From:"))) {
			if (++cnt>num) fputs(line,outfp);
			valid=0;
			}
		}
	else fputs(line,outfp);
	fgets(line,ARR_SIZE-1,infp);
	}
fclose(infp);
fclose(outfp);
unlink(filename);
if (cnt<num) {
	unlink("tempfile");
	sprintf(text,"There were only %d messages in your mailbox, all now deleted.\n",cnt);
	write_user(user,text);
	return;
	}
if (cnt==num) {
	unlink("tempfile"); /* cos it'll be empty anyway */
	write_user(user,"All messages deleted.\n");
	user->room->mesg_cnt=0;
	}
else {
	rename("tempfile",filename);
	sprintf(text,"%d messages deleted.\n",num);
	write_user(user,text);
	}
}


/*** Show list of people your mail is from without seeing the whole lot ***/
mail_from(user)
UR_OBJECT user;
{
FILE *fp;
int valid,cnt;
char w1[ARR_SIZE],line[ARR_SIZE],filename[80];

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) {
	write_user(user,"You have no mail.\n");  return;
	}
write_user(user,"\n~BB*** Mail from ***~RS\n\n");
valid=1;  cnt=0;
fgets(line,DNL,fp); 
fgets(line,ARR_SIZE-1,fp);
while(!feof(fp)) {
	if (*line=='\n') valid=1;
	sscanf(line,"%s",w1);
	if (valid && (!strcmp(w1,"~OLFrom:") || !strcmp(w1,"From:"))) {
		write_user(user,remove_first(line));  
		cnt++;  valid=0;
		}
	fgets(line,ARR_SIZE-1,fp);
	}
fclose(fp);
sprintf(text,"\nTotal of %d messages.\n\n",cnt);
write_user(user,text);
}



/*** Enter user profile ***/
enter_profile(user,done_editing)
UR_OBJECT user;
int done_editing;
{
FILE *fp;
char *c,filename[80];

if (!done_editing) {
	write_user(user,"\n~BB*** Writing profile ***~RS\n\n");
	user->misc_op=5;
	editor(user,NULL,MAX_LINES);
	return;
	}
sprintf(filename,"%s/%s.P",USERFILES,user->name);
if (!(fp=fopen(filename,"w"))) {
	sprintf(text,"%s: couldn't save your profile.\n",syserror);
	write_user(user,text);
	sprintf("ERROR: Couldn't open file %s to write in enter_profile().\n",filename);
	write_syslog(text,0,TOSYS);
	return;
	}
c=user->malloc_start;
while(c!=user->malloc_end) putc(*c++,fp);
fclose(fp);
write_user(user,"Profile stored.\n");
}


/*** Examine a user ***/
examine(user)
UR_OBJECT user;
{
UR_OBJECT u,u2;
FILE *fp;
char filename[80],line[82];
int new_mail,days,hours,mins,timelen,days2,hours2,mins2,idle;
char last_email[80],email_date[10],name[USER_NAME_LEN+1];
char vlast_email[80],vemail_date[10];

vlast_email[0]='\0';
vemail_date[0]='\0';

if (word_count<2) {
	write_user(user,"Examine who?\n");  return;
	}
if (!(u=get_user(word[1]))) {
	if ((u=create_user())==NULL) {
		sprintf(text,"%s: unable to create temporary user object.\n",syserror);
		write_user(user,text);
		write_syslog("ERROR: Unable to create temporary user object in examine().\n",0,TOSYS);
		return;
		}
	strcpy(u->name,word[1]);
	if (!load_user_details(u)) {
		write_user(user,nosuchuser);   
		destruct_user(u);
		destructed=0;
		return;
		}
	u2=NULL;
	}
else u2=u;

sprintf(text,"\n~BB*** %s %s~RS~BB ***~RS\n\n",u->name,u->desc);
write_user(user,text);
sprintf(filename,"%s/%s.P",USERFILES,u->name);
if (!(fp=fopen(filename,"r"))) write_user(user,"No profile.\n");
else {
	fgets(line,81,fp);
	while(!feof(fp)) {
		write_user(user,line);
		fgets(line,81,fp);
		}
	fclose(fp);
	}
sprintf(filename,"%s/%s.M",USERFILES,u->name);
if (!(fp=fopen(filename,"r"))) new_mail=0;
else {
	fscanf(fp,"%d",&new_mail);
	fclose(fp);
	}

days=u->total_login/86400;
hours=(u->total_login%86400)/3600;
mins=(u->total_login%3600)/60;
timelen=(int)(time(0) - u->last_login);
days2=timelen/86400;
hours2=(timelen%86400)/3600;
mins2=(timelen%3600)/60;

if (u2==NULL) {
	sprintf(text,"\nSex        : %c\nLevel      : %s\nLast login : %s",u->sex,level_name[u->path][u->level],ctime((time_t *)&(u->last_login)));
	write_user(user,text);
	sprintf(text,"Which was  : %d days, %d hours, %d minutes ago\n",days2,hours2,mins2);
	write_user(user,text);
	sprintf(text,"Was on for : %d hours, %d minutes\nTotal login: %d days, %d hours, %d minutes\n",u->last_login_len/3600,(u->last_login_len%3600)/60,days,hours,mins);
	write_user(user,text);
	if (user->level>=WIZ) {
		sprintf(text,"Last site  : %s\n",u->last_site);
		write_user(user,text);
		sprintf(filename,"%s/%s.log",LOGDIR,ACCOUNTFILE);
		if(!(fp=fopen(filename,"r"))) {
			strcpy(vlast_email,"<unknown>");
			strcpy(vemail_date,"<unknown>");
			}
		else {
			fscanf(fp,"%s %*s %s %s",email_date,name,last_email);
			while(!feof(fp)) {
				if (!(strcmp(name,u->name))) {
					strcpy(vemail_date,email_date);	
					strcpy(vlast_email,last_email);
					}
				fscanf(fp,"%s %*s %s %s",email_date,name,last_email);
				}
			fclose(fp);
			if (vlast_email[0]=='\0') {
				strcpy(vlast_email,"<unknown>");
				strcpy(vemail_date,"<unknown>");
				}		
			}

		sprintf(text,"E-mail     : %s (%s)\n",vlast_email,vemail_date);
		write_user(user,text);
		}

	if (new_mail>u->read_mail) {
		sprintf(text,"%s has unread mail.\n",u->name);
		write_user(user,text);
		}
	write_user(user,"\n");
	destruct_user(u);
	destructed=0;
	return;
	}
idle=(int)(time(0) - u->last_input)/60;
sprintf(text,"\nSex         : %c\nLevel       : %s\nIgnoring all: %s\n",u->sex,level_name[u->path][u->level],noyes2[u->ignall]);
write_user(user,text);
sprintf(text,"On since    : %sOn for      : %d hours, %d minutes\n",ctime((time_t *)&u->last_login),hours2,mins2);
write_user(user,text);
if (u->afk) {
	sprintf(text,"Idle for    : %d minutes ~BR(AFK)\n",idle);
	write_user(user,text);
	if (u->afk_mesg[0]) {
		sprintf(text,"AFK message : %s\n",u->afk_mesg);
		write_user(user,text);
		}
	}
else {
	sprintf(text,"Idle for    : %d minutes\n",idle);
	write_user(user,text);
	}
sprintf(text,"Total login : %d days, %d hours, %d minutes\n",days,hours,mins);
write_user(user,text);
if (u->socket==-1) {
	sprintf(text,"Home service: %s\n",u->netlink->service);
	write_user(user,text);
	}
else {
	if (user->level>=WIZ) {
		sprintf(text,"Site        : %s:%d\n",u->site,u->site_port);
		write_user(user,text);
		}
	}

if (user->level>=WIZ) {
	sprintf(filename,"%s/%s.log",LOGDIR,ACCOUNTFILE);
	if(!(fp=fopen(filename,"r"))) {
			strcpy(vlast_email,"<unknown>");
			strcpy(vemail_date,"<unknown>");
			}
		else {
			fscanf(fp,"%s %*s %s %s",email_date,name,last_email);
			while(!feof(fp)) {
				if (!(strcmp(name,u->name))) {
					strcpy(vemail_date,email_date);	
					strcpy(vlast_email,last_email);
					}
				fscanf(fp,"%s %*s %s %s",email_date,name,last_email);
				}
			fclose(fp);
			if (vlast_email[0]=='\0') {
				strcpy(vlast_email,"<unknown>");
				strcpy(vemail_date,"<unknown>");
				}		
			}

		sprintf(text,"E-mail      : %s (%s)\n",vlast_email,vemail_date);
		write_user(user,text);
	}

if (new_mail>u->read_mail) {
	sprintf(text,"%s has unread mail.\n",u->name);
	write_user(user,text);
	}
write_user(user,"\n");
}


/*** Show talker rooms ***/
rooms(user,show_topics)
UR_OBJECT user;
int show_topics;
{
RM_OBJECT rm;
UR_OBJECT u;
NL_OBJECT nl;
char access[9],stat[9],serv[SERV_NAME_LEN+1];
int cnt;
char tvar[ROOM_NAME_LEN+1];
if (show_topics) 
	write_user(user,"\n~BB*** Rooms data ***\n\n~FTRoom name            : Access  Users  Mesgs  Topic\n\n");
else write_user(user,"\n~BB*** Rooms data ***\n\n~FTRoom name            : Access  Users  Mesgs  Inlink  LStat  Service~RS\n\n");
for(rm=room_first;rm!=NULL;rm=rm->next) {
	strcpy(tvar,rm->name);
	delump(tvar);
	if (rm->access & PRIVATE) strcpy(access," ~FRPRIV");
	else strcpy(access,"  ~FGPUB");
	if (rm->access & FIXED) access[0]='*';
	cnt=0;
	for(u=user_first;u!=NULL;u=u->next) 
		if (u->type!=CLONE_TYPE && u->room==rm) ++cnt;
	if (show_topics)
		sprintf(text,"%-20s : %9s~RS    %3d    %3d %s\n",tvar,access,cnt,rm->mesg_cnt,rm->topic);
	else {
		nl=rm->netlink;  serv[0]='\0';
		if (nl==NULL) {
			if (rm->inlink) strcpy(stat,"~FRDOWN");
			else strcpy(stat,"   -");
			}
		else {
			if (nl->type==UNCONNECTED) strcpy(stat,"~FRDOWN");
				else if (nl->stage==UP) strcpy(stat,"  ~FGUP");
					else strcpy(stat," ~FYVER");
			}
		if (nl!=NULL) strcpy(serv,nl->service);
		sprintf(text,"%-20s : %9s~RS    %3d    %3d     %s   %s~RS %s\n",tvar,access,cnt,rm->mesg_cnt,noyes1[rm->inlink],stat,serv);
		}
	write_user(user,text);
	}
write_user(user,"\n");
}


/*** List defined netlinks and their status ***/
netstat(user)
UR_OBJECT user;
{
NL_OBJECT nl;
UR_OBJECT u;
char *allow[]={ "  ?","ALL"," IN","OUT" };
char *type[]={ "  -"," IN","OUT" };
char portstr[6],stat[9],vers[8];
int iu,ou,a;

if (nl_first==NULL) {
	write_user(user,"No remote connections configured.\n");  return;
	}
write_user(user,"\n~BB*** Netlink data & status ***~RS\n\n~FTService name : Allow Type Status IU OU Version  Site\n\n");
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	iu=0;  ou=0;
	if (nl->stage==UP) {
		for(u=user_first;u!=NULL;u=u->next) {
			if (u->netlink==nl) {
				if (u->type==REMOTE_TYPE)  ++iu;
				if (u->room==NULL) ++ou;
				}
			}
		}
	if (nl->port) sprintf(portstr,"%d",nl->port);  else portstr[0]='\0';
	if (nl->type==UNCONNECTED) {
		strcpy(stat,"~FRDOWN");  strcpy(vers,"-");
		}
	else {
		if (nl->stage==UP) strcpy(stat,"  ~FGUP");
		else strcpy(stat," ~FYVER");
		if (!nl->ver_major) strcpy(vers,"3.?.?"); /* Pre - 3.2 version */  
		else sprintf(vers,"%d.%d.%d",nl->ver_major,nl->ver_minor,nl->ver_patch);
		}
	/* If link is incoming and remoter vers < 3.2 we have no way of knowing 
	   what the permissions on it are so set to blank */
	if (!nl->ver_major && nl->type==INCOMING && nl->allow!=IN) a=0; 
	else a=nl->allow+1;
	sprintf(text,"%-15s :   %s  %s   %s~RS %2d %2d %7s  %s %s\n",nl->service,allow[a],type[nl->type],stat,iu,ou,vers,nl->site,portstr);
	write_user(user,text);
	}
write_user(user,"\n");
}



/*** Show type of data being received down links (this is usefull when a
     link has hung) ***/
netdata(user)
UR_OBJECT user;
{
NL_OBJECT nl;
char from[80],name[USER_NAME_LEN+1];
int cnt;

cnt=0;
write_user(user,"\n~BB*** Mail receiving status ***~RS\n\n");
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type==UNCONNECTED || nl->mailfile==NULL) continue;
	if (++cnt==1) write_user(user,"To              : From                       Last recv.\n\n");
	sprintf(from,"%s@%s",nl->mail_from,nl->service);
	sprintf(text,"%-15s : %-25s  %d seconds ago.\n",nl->mail_to,from,(int)(time(0)-nl->last_recvd));
	write_user(user,text);
	}
if (!cnt) write_user(user,"No mail being received.\n\n");
else write_user(user,"\n");

cnt=0;
write_user(user,"\n~BB*** Message receiving status ***~RS\n\n");
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type==UNCONNECTED || nl->mesg_user==NULL) continue;
	if (++cnt==1) write_user(user,"To              : From             Last recv.\n\n");
	if (nl->mesg_user==(UR_OBJECT)-1) strcpy(name,"<unknown>");
	else strcpy(name,nl->mesg_user->name);
	sprintf(text,"%-15s : %-15s  %d seconds ago.\n",name,nl->service,(time(0)-nl->last_recvd));
	write_user(user,text);
	}
if (!cnt) write_user(user,"No messages being received.\n\n");
else write_user(user,"\n");
}


/*** Connect a netlink. Use the room as the key ***/
connect_netlink(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;
NL_OBJECT nl;
int ret,tmperr;

if (word_count<2) {
	write_user(user,"Usage: connect <room service is linked to>\n");  return;
	}
inump(inpstr);


if ((rm=get_room(inpstr,user))==NULL) return;
if ((nl=rm->netlink)==NULL) {
	write_user(user,"That room is not linked to a service.\n");
	return;
	}
if (nl->type!=UNCONNECTED) {
	write_user(user,"That rooms netlink is already up.\n");  return;
	}
write_user(user,"Attempting connect (this may cause a temporary hang)...\n");
sprintf(text,"NETLINK: Connection attempt to %s initiated by %s.\n",nl->service,user->name);
write_syslog(text,1,TOSYS);
errno=0;
if (!(ret=connect_to_site(nl))) {
	write_user(user,"~FGInitial connection made...\n");
	sprintf(text,"NETLINK: Connected to %s (%s %d).\n",nl->service,nl->site,nl->port);
	write_syslog(text,1,TOSYS);
	nl->connect_room=rm;
	return;
	}
tmperr=errno; /* On Linux errno seems to be reset between here and sprintf */
write_user(user,"~FRConnect failed: ");
write_syslog("NETLINK: Connection attempt failed: ",1,TOSYS);
if (ret==1) {
	sprintf(text,"%s.\n",sys_errlist[tmperr]);
	write_user(user,text);
	write_syslog(text,0,TOSYS);
	return;
	}
write_user(user,"Unknown hostname.\n");
write_syslog("Unknown hostname.\n",0,TOSYS);
}



/*** Disconnect a link ***/
disconnect_netlink(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;
NL_OBJECT nl;
char tvar[ROOM_NAME_LEN+1];

if (word_count<2) {
	write_user(user,"Usage: disconnect <room service is linked to>\n");  return;
	}
inump(inpstr);

if ((rm=get_room(inpstr,user))==NULL) return;

strcpy(tvar,rm->name);
delump(tvar);

nl=rm->netlink;
if (nl==NULL) {
	write_user(user,"That room is not linked to a service.\n");
	return;
	}
if (nl->type==UNCONNECTED) {
	write_user(user,"That rooms netlink is not connected.\n");  return;
	}
/* If link has hung at verification stage don't bother announcing it */
if (nl->stage==UP) {
	sprintf(text,"~OLSYSTEM:~RS Disconnecting from %s in the %s.\n",nl->service,tvar);
	write_room(NULL,text);
	sprintf(text,"NETLINK: Link to %s in the %s disconnected by %s.\n",nl->service,rm->name,user->name);
	write_syslog(text,1,TOSYS);
	}
else {
	sprintf(text,"NETLINK: Link to %s disconnected by %s.\n",nl->service,user->name);
	write_syslog(text,1,TOSYS);
	}
shutdown_netlink(nl);
write_user(user,"Disconnected.\n");
}


/*** Change users password. Only ARCHes and above can change another users 
	password and they do this by specifying the user at the end. When this is 
	done the old password given can be anything, the wiz doesnt have to know it
	in advance. ***/
change_pass(user)
UR_OBJECT user;
{
UR_OBJECT u;
char *name;

if (word_count<3) {
	if (user->level<GOD)
		write_user(user,"Usage: passwd <old password> <new password>\n");
	else write_user(user,"Usage: passwd <old password> <new password> [<user>]\n");
	return;
	}
if (strlen(word[2])<3) {
	write_user(user,"New password too short.\n");  return;
	}
if (strlen(word[2])>PASS_LEN) {
	write_user(user,"New password too long.\n");  return;
	}
/* Change own password */
if (word_count==3) {
	if (strcmp((char *)crypt(word[1],"NU"),user->pass)) {
		write_user(user,"Old password incorrect.\n");  return;
		}
	if (!strcmp(word[1],word[2])) {
		write_user(user,"Old and new passwords are the same.\n");  return;
		}
	strcpy(user->pass,(char *)crypt(word[2],"NU"));
	save_user_details(user,0);
	cls(user);
	write_user(user,"Password changed.\n");
	return;
	}
/* Change someone elses */
if (user->level<GOD) {
	write_user(user,"You are not a high enough level to use the user option.\n");  
	return;
	}
word[3][0]=toupper(word[3][0]);
if (!strcmp(word[3],user->name)) {
	/* security feature  - prevents someone coming to a wizes terminal and 
	   changing his password since he wont have to know the old one */
	write_user(user,"You cannot change your own password using the <user> option.\n");
	return;
	}
if (u=get_user(word[3])) {
	if (u->type==REMOTE_TYPE) {
		write_user(user,"You cannot change the password of a user logged on remotely.\n");
		return;
		}
	if (u->level>=user->level) {
		write_user(user,"You cannot change the password of a user of equal or higher level than yourself.\n");
		return;
		}
	strcpy(u->pass,(char *)crypt(word[2],"NU"));
	cls(user);
	sprintf(text,"%s's password has been changed.\n",u->name);
	write_user(user,text);
	if (user->vis) name=user->name; else name=invisname;
	sprintf(text,"~FR~OLYour password has been changed by %s!\n",name);
	write_user(u,text);
	sprintf(text,"%s changed %s's password.\n",user->name,u->name);
	write_syslog(text,1,TOSYS);
	return;
	}
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in change_pass().\n",0,TOSYS);
	return;
	}
strcpy(u->name,word[3]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);   
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot change the password of a user of equal or higher level than yourself.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
strcpy(u->pass,(char *)crypt(word[2],"NU"));
save_user_details(u,0);
destruct_user(u);
destructed=0;
cls(user);
sprintf(text,"%s's password changed to \"%s\".\n",word[3],word[2]);
write_user(user,text);
sprintf(text,"%s changed %s's password.\n",user->name,u->name);
write_syslog(text,1,TOSYS);
}


/*** Kill a user ***/
kill_user(user)
UR_OBJECT user;
{
UR_OBJECT victim;
RM_OBJECT rm;
char *name;

if (word_count<2) {
	write_user(user,"Usage: kill <user>\n");  return;
	}
if (!(victim=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (user==victim) {
	write_user(user,"Trying to commit suicide this way is the sixth sign of madness.\n");
	return;
	}
if (victim->level>=user->level) {
	write_user(user,"You cannot kill a user of equal or higher level than yourself.\n");
	sprintf(text,"%s tried to kill you!\n",user->name);
	write_user(victim,text);
	return;
	}
sprintf(text,"%s KILLED %s.\n",user->name,victim->name);
write_syslog(text,1,TOSYS);
write_user(user,"~FM~OLYou chant an evil incantation...\n");
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~FM~OL%s chants an evil incantation...\n",name);
write_room_except(user->room,text,user);
write_user(victim,"~FM~OLA shrieking furie rises up out of the ground, and devours you!!!\n");
sprintf(text,"~FM~OLA shrieking furie rises up out of the ground, devours %s and vanishes!!!\n",victim->name);
rm=victim->room;
write_room_except(rm,text,victim);
disconnect_user(victim);
write_room(NULL,"~FM~OLYou hear insane laughter from the beyond the grave...\n");
}


/*** Promote a user ***/
promote(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
char text2[80],*name;

if (word_count<2) {
	write_user(user,"Usage: promote <user>\n");  return;
	}
/* See if user is on atm */
if ((u=get_user(word[1]))!=NULL) {
	if (u->level>=user->level) {
		write_user(user,"You cannot promote a user to a level higher than your own.\n");
		return;
		}
	if (user->vis) name=user->name; else name=invisname;
	u->level++;
	sprintf(text,"~FG~OLYou promote %s to level: ~RS~OL%s.\n",u->name,level_name[u->path][u->level]);
	write_user(user,text);
	rm=user->room;
	user->room=NULL;
	sprintf(text,"~FG~OL%s promotes %s to level: ~RS~OL%s.\n",name,u->name,level_name[u->path][u->level]);
	write_room_except(NULL,text,u);
	user->room=rm;
	sprintf(text,"~FG~OL%s has promoted you to level: ~RS~OL%s!\n",name,level_name[u->path][u->level]);
	write_user(u,text);
	sprintf(text,"%s PROMOTED %s to level %s.\n",name,u->name,level_name[u->path][u->level]);
	write_syslog(text,1,TOSYS);
	return;
	}
/* Create a temp session, load details, alter , then save. This is inefficient
   but its simpler than the alternative */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in promote().\n",0,TOSYS);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot promote a user to a level higher than your own.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
u->level++;  
u->socket=-2;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"You promote %s to level:~OL%s.\n",u->name,level_name[u->path][u->level]);
write_user(user,text);
sprintf(text2,"~FG~OLYou have been promoted to level: ~RS~OL%s.\n",level_name[u->path][u->level]);
send_mail(user,word[1],text2);
sprintf(text,"%s PROMOTED %s to level %s.\n",user->name,word[1],level_name[u->path][u->level]);
write_syslog(text,1,TOSYS);
destruct_user(u);
destructed=0;
}


/*** Demote a user ***/
demote(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
char text2[80],*name;

if (word_count<2) {
	write_user(user,"Usage: demote <user>\n");  return;
	}
/* See if user is on atm */
if ((u=get_user(word[1]))!=NULL) {
	if (u->level==NEW) {
		write_user(user,"You cannot demote a user of level NEW.\n");
		return;
		}
	if (u->level>=user->level) {
		write_user(user,"You cannot demote a user of an equal or higher level than yourself.\n");
		return;
		}
	if (user->vis) name=user->name; else name=invisname;
	u->level--;
	if (u->level<USER) u->path=0;
	sprintf(text,"~FR~OLYou demote %s to level: ~RS~OL%s.\n",u->name,level_name[u->path][u->level]);
	write_user(user,text);
	rm=user->room;
	user->room=NULL;
	sprintf(text,"~FR~OL%s demotes %s to level: ~RS~OL%s.\n",name,u->name,level_name[u->path][u->level]);
	write_room_except(NULL,text,u);
	user->room=rm;
	sprintf(text,"~FR~OL%s has demoted you to level: ~RS~OL%s!\n",name,level_name[u->path][u->level]);
	write_user(u,text);
	sprintf(text,"%s DEMOTED %s to level %s.\n",user->name,u->name,level_name[u->path][u->level]);
	write_syslog(text,1,TOSYS);
	return;
	}
/* User not logged on */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in demote().\n",0,TOSYS);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level==NEW) {
	write_user(user,"You cannot demote a user of level NEW.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot demote a user of an equal or higher level than yourself.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
u->level--;
if (u->level<USER) u->path=0;
u->socket=-2;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"You demote %s to level: ~OL%s.\n",u->name,level_name[u->path][u->level]);
write_user(user,text);
sprintf(text2,"~FR~OLYou have been demoted to level: ~RS~OL%s.\n",level_name[u->path][u->level]);
send_mail(user,word[1],text2);
sprintf(text,"%s DEMOTED %s to level %s.\n",user->name,word[1],level_name[u->path][u->level]);
write_syslog(text,1,TOSYS);
destruct_user(u);
destructed=0;
}


/*** List banned sites or users ***/
listbans(user)
UR_OBJECT user;
{
int i;
char filename[80];

if (!strcmp(word[1],"sites")) {
	write_user(user,"\n~BB*** Banned sites/domains ***~RS\n\n"); 
	sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
	switch(more(user,user->socket,filename)) {
		case 0:
		write_user(user,"There are no banned sites/domains.\n\n");
		return;

		case 1: user->misc_op=2;
		}
	return;
	}
if (!strcmp(word[1],"users")) {
	write_user(user,"\n~BB*** Banned users ***~RS\n\n");
	sprintf(filename,"%s/%s",DATAFILES,USERBAN);
	switch(more(user,user->socket,filename)) {
		case 0:
		write_user(user,"There are no banned users.\n\n");
		return;

		case 1: user->misc_op=2;
		}
	return;
	}
if (!strcmp(word[1],"swears")) {
	write_user(user,"\n~BB*** Banned swear words ***~RS\n\n");
	i=0;
	while(swear_words[i][0]!='*') {
		write_user(user,swear_words[i]);
		write_user(user,"\n");
		++i;
		}
	if (!i) write_user(user,"There are no banned swear words.\n");
	if (ban_swearing) write_user(user,"\n");
	else write_user(user,"\n(Swearing ban is currently off)\n\n");
	return;
	}
write_user(user,"Usage: listbans sites/users/swears\n"); 
}


/*** Ban a site/domain or user ***/
ban(user)
UR_OBJECT user;
{
char *usage="Usage: ban site/user <site/user name>\n";

if (word_count<3) {
	write_user(user,usage);  return;
	}
if (!strcmp(word[1],"site")) {  ban_site(user);  return;  }
if (!strcmp(word[1],"user")) {  ban_user(user);  return;  }
write_user(user,usage);
}


ban_site(user)
UR_OBJECT user;
{
FILE *fp;
char filename[80],host[81],site[80];

gethostname(host,80);
if (!strcmp(word[2],host)) {
	write_user(user,"You cannot ban the machine that this program is running on.\n");
	return;
	}
sprintf(filename,"%s/%s",DATAFILES,SITEBAN);

/* See if ban already set for given site */
if (fp=fopen(filename,"r")) {
	fscanf(fp,"%s",site);
	while(!feof(fp)) {
		if (!strcmp(site,word[2])) {
			write_user(user,"That site/domain is already banned.\n");
			fclose(fp);  return;
			}
		fscanf(fp,"%s",site);
		}
	fclose(fp);
	}

/* Write new ban to file */
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: Can't open file to append.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open file to append in ban_site().\n",0,TOSYS);
	return;
	}
fprintf(fp,"%s\n",word[2]);
fclose(fp);
write_user(user,"Site/domain banned.\n");
sprintf(text,"%s BANNED site/domain %s.\n",user->name,word[2]);
write_syslog(text,1,TOSYS);
}


ban_user(user)
UR_OBJECT user;
{
UR_OBJECT u;
FILE *fp;
char filename[80],filename2[80],p[20],name[USER_NAME_LEN+1];
int a,b,c,d,level;

sprintf(filename,"%s/%s",DATAFILES,USERBAN);

if ((u=get_user(word[2]))!=NULL) { /* loggato */

	if (u==user) {
	        write_user(user,"Trying to ban yourself is the seventh sign of madness.\n");
		return;
	        }


        if (u->level>=user->level) {
                write_user(user,"You cannot ban a user of equal or higher level than yourself.\n");
		return;
                }

	if (!(fp=fopen(filename,"a"))) {
		sprintf(text,"%s: Can't open file to append.\n",syserror);
		write_user(user,text);
		write_syslog("ERROR: Couldn't open file to append in ban_user().\n",0,TOSYS);
		return;
        	}

	fprintf(fp,"%s\n",u->name);
	fclose(fp);
	write_user(user,"User banned.\n");
	sprintf(text,"%s BANNED user %s.\n",user->name,u->name);
	write_syslog(text,1,TOSYS);
        write_user(u,"\n\07~FR~OL~LIYou have been banned from here!\n\n");
        disconnect_user(u);
	return;
	}


/* see if user is not already banned */

if (fp=fopen(filename,"r")) {
	fscanf(fp,"%s",name);
	while(!feof(fp)) {
		if (!strcmp(name,word[2])) {
			write_user(user,"That user is already banned.\n");
			fclose(fp);  return;
			}
		fscanf(fp,"%s",name);
		}
	fclose(fp);
	}

if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in ban_user()\n");
	return;
       	}

strcpy(u->name,word[2]);
	
if (!load_user_details(u)) {
	write_user(user,nosuchuser);
	destruct_user(u);
	destructed=0;
	return;
	}
	
if (u->level>=user->level) {
	write_user(user,"You cannot ban a user of a level higher or equal than your own.\n");
	destruct_user(u);
	destructed=0;
	return;
        }
	
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: Can't open file to append.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open file to append in ban_user().\n",0,TOSYS);
	return;
	}

fprintf(fp,"%s\n",u->name);
fclose(fp);

write_user(user,"User banned.\n");
sprintf(text,"%s BANNED user %s.\n",user->name,u->name);
write_syslog(text,1,TOSYS);
destruct_user(u);
destructed=0;
}

	

/*** uban a site (or domain) or user ***/
unban(user)
UR_OBJECT user;
{
char *usage="Usage: unban site/user <site/user name>\n";

if (word_count<3) {
	write_user(user,usage);  return;
	}
if (!strcmp(word[1],"site")) {  unban_site(user);  return;  }
if (!strcmp(word[1],"user")) {  unban_user(user);  return;  }
write_user(user,usage);
}


unban_site(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
char filename[80],site[80];
int found,cnt;

sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"That site/domain is not currently banned.\n");
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: Couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile to write in unban_site().\n",0,TOSYS);
	fclose(infp);
	return;
	}
found=0;   cnt=0;
fscanf(infp,"%s",site);
while(!feof(infp)) {
	if (strcmp(word[2],site)) {  
		fprintf(outfp,"%s\n",site);  cnt++;  
		}
	else found=1;
	fscanf(infp,"%s",site);
	}
fclose(infp);
fclose(outfp);
if (!found) {
	write_user(user,"That site/domain is not currently banned.\n");
	unlink("tempfile");
	return;
	}
if (!cnt) {
	unlink(filename);  unlink("tempfile");
	}
else rename("tempfile",filename);
write_user(user,"Site ban removed.\n");
sprintf(text,"%s UNBANNED site %s.\n",user->name,word[2]);
write_syslog(text,1,TOSYS);
}


unban_user(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
char filename[80],name[USER_NAME_LEN+1];
int found,cnt;

sprintf(filename,"%s/%s",DATAFILES,USERBAN);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"That user is not currently banned.\n");
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: Couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile to write in unban_user().\n",0,TOSYS);
	fclose(infp);
	return;
	}
found=0;  cnt=0;
word[2][0]=toupper(word[2][0]);
fscanf(infp,"%s",name);
while(!feof(infp)) {
	if (strcmp(word[2],name)) {
		fprintf(outfp,"%s\n",name);  cnt++;
		}
	else found=1;
	fscanf(infp,"%s",name);
	}
fclose(infp);
fclose(outfp);
if (!found) {
	write_user(user,"That user is not currently banned.\n");
	unlink("tempfile");
	return;
	}
if (!cnt) {
	unlink(filename);  unlink("tempfile");
	}
else rename("tempfile",filename);
write_user(user,"User ban removed.\n");
sprintf(text,"%s UNBANNED user %s.\n",user->name,word[2]);
write_syslog(text,1,TOSYS);
}



/*** Set user visible or invisible ***/
visibility(user,vis)
UR_OBJECT user;
int vis;
{
if (vis) {
	if (user->vis) {
		write_user(user,"You are already visible.\n");  return;
		}
	write_user(user,"~FB~OLYou recite a melodic incantation and reappear.\n");
	sprintf(text,"~FB~OLYou hear a melodic incantation chanted and %s materialises!\n",user->name);
	write_room_except(user->room,text,user);
	user->vis=1;
	return;
	}
if (!user->vis) {
	write_user(user,"You are already invisible.\n");  return;
	}
write_user(user,"~FB~OLYou recite a melodic incantation and fade out.\n");
sprintf(text,"~FB~OL%s recites a melodic incantation and disappears!\n",user->name);
write_room_except(user->room,text,user);
user->vis=0;
}


/*** Site a user ***/
site(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_user(user,"Usage: site <user>\n");  return;
	}
/* User currently logged in */
if (u=get_user(word[1])) {
	if (u->type==REMOTE_TYPE) sprintf(text,"%s is remotely connected from %s.\n",u->name,u->site);
	else sprintf(text,"%s is logged in from %s:%d.\n",u->name,u->site,u->site_port);
	write_user(user,text);
	return;
	}
/* User not logged in */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in site().\n",0,TOSYS);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);
	destruct_user(u);
	destructed=0;
	return;
	}
sprintf(text,"%s was last logged in from %s.\n",word[1],u->last_site);
write_user(user,text);
destruct_user(u);
destructed=0;
}


/*** Wake up some sleepy herbert ***/
wake(user)
UR_OBJECT user;
{
UR_OBJECT u;
char *name;

if (word_count<2) {
	write_user(user,"Usage: wake <user>\n");  return;
	}
if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot wake anyone.\n");  return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (u==user) {
	write_user(user,"Trying to wake yourself up is the eighth sign of madness.\n");
	return;
	}
if (u->afk) {
	write_user(user,"You cannot wake someone who is AFK.\n");  return;
	}
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"\07\n~BR*** %s says: ~OL~LIWAKE UP!!!~RS~BR ***\n\n",name);
write_user(u,text);
write_user(user,"Wake up call sent.\n");
}


/*** Shout something to other wizes and gods. If the level isnt given it
	defaults to WIZ level. ***/
wizshout(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int lev;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot wizshout.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Usage: wizshout [<superuser level>] <message>\n"); 
	return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
strtoupper(word[1]);
if ((lev=get_level(word[1]))==-1) lev=WIZ;
else {
	if (lev<WIZ || word_count<3) {
		write_user(user,"Usage: wizshout [<superuser level>] <message>\n");
		return;
		}
	if (lev>user->level) {
		write_user(user,"You cannot specifically shout to users of a higher level than yourself.\n");
		return;
		}
	inpstr=remove_first(inpstr);
	sprintf(text,"~OLYou wizshout to level %s:~RS %s\n",level_name[user->path][lev],inpstr);
	write_user(user,text);
	sprintf(text,"~OL%s wizshouts to level %s:~RS %s\n",user->name,level_name[user->path][lev],inpstr);
	write_level(lev,1,text,user);
	return;
	}
sprintf(text,"~OLYou wizshout:~RS %s\n",inpstr);
write_user(user,text);
sprintf(text,"~OL%s wizshouts:~RS %s\n",user->name,inpstr);
write_level(WIZ,1,text,user);
}


/*** Muzzle an annoying user so he cant speak, emote, echo, write, smail
	or bcast. Muzzles have levels from WIZ to GOD so for instance a wiz
     cannot remove a muzzle set by a god.  ***/
muzzle(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_user(user,"Usage: muzzle <user>\n");  return;
	}
if ((u=get_user(word[1]))!=NULL) {
	if (u==user) {
		write_user(user,"Trying to muzzle yourself is the ninth sign of madness.\n");
		return;
		}
	if (u->level>=user->level) {
		write_user(user,"You cannot muzzle a user of equal or higher level than yourself.\n");
		return;
		}
	if (u->muzzled>=user->level) {
		sprintf(text,"%s is already muzzled.\n",u->name);
		write_user(user,text);  return;
		}
	sprintf(text,"~FR~OL%s now has a muzzle of level: ~RS~OL%s.\n",u->name,level_name[user->path][user->level]);
	write_user(user,text);
	write_user(u,"~FR~OLYou have been muzzled!\n");
	sprintf(text,"%s muzzled %s.\n",user->name,u->name);
	write_syslog(text,1,TOSYS);
	u->muzzled=user->level;
	return;
	}
/* User not logged on */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in muzzle().\n",0,TOSYS);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot muzzle a user of equal or higher level than yourself.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->muzzled>=user->level) {
	sprintf(text,"%s is already muzzled.\n",u->name);
	write_user(user,text); 
	destruct_user(u);
	destructed=0;
	return;
	}
u->socket=-2;
u->muzzled=user->level;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"~FR~OL%s given a muzzle of level: ~RS~OL%s.\n",u->name,level_name[user->path][user->level]);
write_user(user,text);
send_mail(user,word[1],"~FR~OLYou have been muzzled!\n");
sprintf(text,"%s muzzled %s.\n",user->name,u->name);
write_syslog(text,1,TOSYS);
destruct_user(u);
destructed=0;
}



/*** Umuzzle the bastard now he's apologised and grovelled enough via email ***/
unmuzzle(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_user(user,"Usage: unmuzzle <user>\n");  return;
	}
if ((u=get_user(word[1]))!=NULL) {
	if (u==user) {
		write_user(user,"Trying to unmuzzle yourself is the tenth sign of madness.\n");
		return;
		}
	if (!u->muzzled) {
		sprintf(text,"%s is not muzzled.\n",u->name);  return;
		}
	if (u->muzzled>user->level) {
		sprintf(text,"%s's muzzle is set to level %s, you do not have the power to remove it.\n",u->name,level_name[user->path][u->muzzled]);
		write_user(user,text);  return;
		}
	sprintf(text,"~FG~OLYou remove %s's muzzle.\n",u->name);
	write_user(user,text);
	write_user(u,"~FG~OLYou have been unmuzzled!\n");
	sprintf(text,"%s unmuzzled %s.\n",user->name,u->name);
	write_syslog(text,1,TOSYS);
	u->muzzled=0;
	return;
	}
/* User not logged on */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in unmuzzle().\n",0,TOSYS);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->muzzled>user->level) {
	sprintf(text,"%s's muzzle is set to level %s, you do not have the power to remove it.\n",u->name,level_name[user->path][u->muzzled]);
	write_user(user,text);  
	destruct_user(u);
	destructed=0;
	return;
	}
u->socket=-2;
u->muzzled=0;
strcpy(u->site,u->last_site);
save_user_details(u,0);
sprintf(text,"~FG~OLYou remove %s's muzzle.\n",u->name);
write_user(user,text);
send_mail(user,word[1],"~FG~OLYou have been unmuzzled.\n");
sprintf(text,"%s unmuzzled %s.\n",user->name,u->name);
write_syslog(text,1,TOSYS);
destruct_user(u);
destructed=0;
}



/*** Switch system logging on and off ***/
logging(user)
UR_OBJECT user;
{
if (system_logging) {
	write_user(user,"System logging ~FROFF.\n");
	sprintf(text,"%s switched system logging OFF.\n",user->name);
	write_syslog(text,1,TOSYS);
	system_logging=0;
	return;
	}
system_logging=1;
write_user(user,"System logging ~FGON.\n");
sprintf(text,"%s switched system logging ON.\n",user->name);
write_syslog(text,1,TOSYS);
}


/*** Set minlogin level ***/
minlogin(user)
UR_OBJECT user;
{
UR_OBJECT u,next;
char *usage="Usage: minlogin NONE/<user level>\n";
char levstr[5],*name;
int lev,cnt;

if (word_count<2) {
	write_user(user,usage);  return;
	}
strtoupper(word[1]);
if ((lev=get_level(word[1]))==-1) {
	if (strcmp(word[1],"NONE")) {
		write_user(user,usage);  return;
		}
	lev=-1;
	strcpy(levstr,"NONE");
	}
else strcpy(levstr,level_name[user->path][lev]);
if (lev>user->level) {
	write_user(user,"You cannot set minlogin to a higher level than your own.\n");
	return;
	}
if (minlogin_level==lev) {
	write_user(user,"It is already set to that.\n");  return;
	}
minlogin_level=lev;
sprintf(text,"Minlogin level set to: ~OL%s.\n",levstr);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s has set the minlogin level to: ~OL%s.\n",name,levstr);
write_room_except(NULL,text,user);
sprintf(text,"%s set the minlogin level to %s.\n",user->name,levstr);
write_syslog(text,1,TOSYS);

/* Now boot off anyone below that level */
cnt=0;
u=user_first;
while(u) {
	next=u->next;
	if (!u->login && u->type!=CLONE_TYPE && u->level<lev) {
		write_user(u,"\n~FY~OLYour level is now below the minlogin level, disconnecting you...\n");
		disconnect_user(u);
		++cnt;
		}
	u=next;
	}
sprintf(text,"Total of %d users were disconnected.\n",cnt);
destructed=0;
write_user(user,text);
}



/*** Show talker system parameters etc ***/
system_details(user)
UR_OBJECT user;
{
NL_OBJECT nl;
RM_OBJECT rm;
UR_OBJECT u;
char bstr[40],minlogin[5];
char *ca[]={ "NONE  ","IGNORE","REBOOT" };
int days,hours,mins,secs;
int netlinks,live,inc,outg;
int rms,inlinks,num_clones,mem,size;

sprintf(text,"\n~BB*** NUTS version %s - System status ***~RS\n\n",VERSION);
write_user(user,text);

/* Get some values */
strcpy(bstr,ctime(&boot_time));
secs=(int)(time(0)-boot_time);
days=secs/86400;
hours=(secs%86400)/3600;
mins=(secs%3600)/60;
secs=secs%60;
num_clones=0;
mem=0;
size=sizeof(struct user_struct);
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE) num_clones++;
	mem+=size;
	}

rms=0;  
inlinks=0;
size=sizeof(struct room_struct);
for(rm=room_first;rm!=NULL;rm=rm->next) {
	if (rm->inlink) ++inlinks;
	++rms;  mem+=size;
	}

netlinks=0;  
live=0;
inc=0; 
outg=0;
size=sizeof(struct netlink_struct);
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type!=UNCONNECTED && nl->stage==UP) live++;
	if (nl->type==INCOMING) ++inc;
	if (nl->type==OUTGOING) ++outg;
	++netlinks;  mem+=size;
	}
if (minlogin_level==-1) strcpy(minlogin,"NONE");
else strcpy(minlogin,level_name[user->path][minlogin_level]);

/* Show header parameters */
sprintf(text,"~FTProcess ID   : ~FG%d\n~FTTalker booted: ~FG%s~FTUptime       : ~FG%d days, %d hours, %d minutes, %d seconds\n",getpid(),bstr,days,hours,mins,secs);
write_user(user,text);
sprintf(text,"~FTPorts (M/W/L): ~FG%d,  %d,  %d\n\n",port[0],port[1],port[2]);
write_user(user,text);

/* Show others */
sprintf(text,"Max users              : %-3d          Current num. of users  : %d\n",max_users,num_of_users);
write_user(user,text);
sprintf(text,"Max clones             : %-2d           Current num. of clones : %d\n",max_clones,num_clones);
write_user(user,text);
sprintf(text,"Current minlogin level : %-4s         Login idle time out    : %d secs.\n",minlogin,login_idle_time);
write_user(user,text);
sprintf(text,"User idle time out     : %-4d secs.   Heartbeat              : %d\n",user_idle_time,heartbeat);
write_user(user,text);
sprintf(text,"Remote user maxlevel   : %-4s         Remote user deflevel   : %s\n",level_name[user->path][rem_user_maxlevel],level_name[user->path][rem_user_deflevel]);
write_user(user,text);
sprintf(text,"Wizport min login level: %-4s         Gatecrash level        : %s\n",level_name[user->path][wizport_level],level_name[user->path][gatecrash_level]);
write_user(user,text);
sprintf(text,"Time out maxlevel      : %-4s         Private room min count : %d\n",level_name[user->path][time_out_maxlevel],min_private_users);
write_user(user,text);
sprintf(text,"Message lifetime       : %-2d days      Message check time     : %02d:%02d\n",mesg_life,mesg_check_hour,mesg_check_min);
write_user(user,text);
sprintf(text,"Net idle time out      : %-4d secs.   Number of rooms        : %d\n",net_idle_time,rms);
write_user(user,text);
sprintf(text,"Num. accepting connects: %-2d           Total netlinks         : %d\n",inlinks,netlinks);
write_user(user,text);
sprintf(text,"Number which are live  : %-2d           Number incoming        : %d\n",live,inc);
write_user(user,text);
sprintf(text,"Number outgoing        : %-2d           Ignoring sigterm       : %s\n",outg,noyes2[ignore_sigterm]);
write_user(user,text);
sprintf(text,"Echoing passwords      : %s          Swearing banned        : %s\n",noyes2[password_echo],noyes2[ban_swearing]);
write_user(user,text);
sprintf(text,"Time out afks          : %s          Allowing caps in name  : %s\n",noyes2[time_out_afks],noyes2[allow_caps_in_name]);
write_user(user,text);
sprintf(text,"New user prompt default: %s          New user colour default: %s\n",offon[prompt_def],offon[colour_def]);
write_user(user,text);
sprintf(text,"New user charecho def. : %s          System logging         : %s\n",offon[charecho_def],offon[system_logging]);
write_user(user,text);
sprintf(text,"Crash action           : %s       Object memory allocated: %d\n\n",ca[crash_action],mem);
write_user(user,text);
}


/*** Set the character mode echo on or off. This is only for users logging in
     via a character mode client, those using a line mode client (eg unix
     telnet) will see no effect. ***/
toggle_charecho(user)
UR_OBJECT user;
{
if (!user->charmode_echo) {
	write_user(user,"Echoing for character mode clients ~FGON.\n");
	user->charmode_echo=1;
	}
else {
	write_user(user,"Echoing for character mode clients ~FROFF.\n");
	user->charmode_echo=0;
	}
if (user->room==NULL) prompt(user);
}


/*** Free a hung socket ***/
clearline(user)
UR_OBJECT user;
{
UR_OBJECT u;
int sock;

if (word_count<2 || !isnumber(word[1])) {
	write_user(user,"Usage: clearline <line>\n");  return;
	}
sock=atoi(word[1]);

/* Find line amongst users */
for(u=user_first;u!=NULL;u=u->next) 
	if (u->type!=CLONE_TYPE && u->socket==sock) goto FOUND;
write_user(user,"That line is not currently active.\n");
return;

FOUND:
if (!u->login) {
	write_user(user,"You cannot clear the line of a logged in user.\n");
	return;
	}
write_user(u,"\n\nThis line is being cleared.\n\n");
disconnect_user(u); 
sprintf(text,"%s cleared line %d.\n",user->name,sock);
write_syslog(text,1,TOSYS);
sprintf(text,"Line %d cleared.\n",sock);
write_user(user,text);
destructed=0;
no_prompt=0;
}


/*** Change whether a rooms access is fixed or not ***/
change_room_fix(user,fix,inpstr)
UR_OBJECT user;
int fix;
char *inpstr;
{
RM_OBJECT rm;
char *name;
char tvar[ROOM_NAME_LEN+1];


if (word_count<2) rm=user->room;
else {
	inump(inpstr);
	if ((rm=get_room(inpstr,user))==NULL) return;
	}
strcpy(tvar,rm->name);
delump(tvar);

if (user->vis) name=user->name; else name=invisname;
if (fix) {	
	if (rm->access & FIXED) {
		if (rm==user->room) 
			write_user(user,"This room's access is already fixed.\n");
		else write_user(user,"That room's access is already fixed.\n");
		return;
		}
	sprintf(text,"Access for room %s is now ~FRFIXED.\n",tvar);
	write_user(user,text);
	if (user->room==rm) {
		sprintf(text,"%s has ~FRFIXED~RS access for this room.\n",name);
		write_room_except(rm,text,user);
		}
	else {
		sprintf(text,"This room's access has been ~FRFIXED.\n");
		write_room(rm,text);
		}
	sprintf(text,"%s FIXED access to room %s.\n",user->name,tvar);
	write_syslog(text,1,TOSYS);
	rm->access+=2;
	return;
	}
if (!(rm->access & FIXED)) {
	if (rm==user->room) 
		write_user(user,"This room's access is already unfixed.\n");
	else write_user(user,"That room's access is already unfixed.\n");
	return;
	}
sprintf(text,"Access for room %s is now ~FGUNFIXED.\n",tvar);
write_user(user,text);
if (user->room==rm) {
	sprintf(text,"%s has ~FGUNFIXED~RS access for this room.\n",name);
	write_room_except(rm,text,user);
	}
else {
	sprintf(text,"This room's access has been ~FGUNFIXED.\n");
	write_room(rm,text);
	}
sprintf(text,"%s UNFIXED access to room %s.\n",user->name,tvar);
write_syslog(text,1,TOSYS);
rm->access-=2;
reset_access(rm);
}



/*** View the system log ***/
viewlog(user)
UR_OBJECT user;
{
FILE *fp;
char c,*emp="\nThe system log is empty.\n";
int lines,cnt,cnt2;
char filename[80];
char logtype[30];

switch (word[2][0]) {
	case 'r' : sprintf(filename,"%s/%s.log",LOGDIR,ROOMFILE); 
		   strcpy(logtype,"Room"); break;
	case 'a' : sprintf(filename,"%s/%s.log",LOGDIR,ACCOUNTFILE); 
		   strcpy(logtype,"Account"); break;
	 default : sprintf(filename,"%s/%s.log",LOGDIR,SYSLOG); 
		   strcpy(logtype,"System"); break;
	}


if (!(strcmp(word[1],"all"))) {
	sprintf(text,"\n~BB*** %s log ***~RS\n\n",logtype);
	write_user(user,text);
	switch(more(user,user->socket,filename)) {
		case 0: write_user(user,emp);  return;
		case 1: user->misc_op=2; 
		}
	return;
	}

if ((lines=atoi(word[1]))<1) {
	write_user(user,"Usage: viewlog [<lines from the end>/all] (r/a/s)\n"); return;
	}
/* Count total lines */
if (!(fp=fopen(filename,"r"))) {  write_user(user,emp);  return;  }
cnt=0;

c=getc(fp);
while(!feof(fp)) {
	if (c=='\n') ++cnt;
	c=getc(fp);
	}
if (cnt<lines) {
	sprintf(text,"There are only %d lines in the log.\n",cnt);
	write_user(user,text);
	fclose(fp);
	return;
	}
if (cnt==lines) {
	sprintf(text,"\n~BB*** %s log ***~RS\n\n",logtype);
	write_user(user,text);
	fclose(fp);  more(user,user->socket,filename);  return;
	}

/* Find line to start on */
fseek(fp,0,0);
cnt2=0;
c=getc(fp);
while(!feof(fp)) {
	if (c=='\n') ++cnt2;
	c=getc(fp);
	if (cnt2==cnt-lines) {
		sprintf(text,"\n~BB*** %s log (last %d lines) ***~RS\n\n",logtype,lines);
		write_user(user,text);
		user->filepos=ftell(fp)-1;
		fclose(fp);
		if (more(user,user->socket,filename)!=1) user->filepos=0;
		else user->misc_op=2;
		return;
		}
	}
fclose(fp);
sprintf(text,"%s: Line count error.\n",syserror);
write_user(user,text);
write_syslog("ERROR: Line count error in viewlog().\n",0,TOSYS);
}


/*** A newbie is requesting an account. Get his email address off him so we
     can validate who he is before we promote him and let him loose as a 
     proper user. ***/
account_request(user)
UR_OBJECT user;
{

/* This is so some pillock doesnt keep doing it just to fill up the syslog */
if (user->accreq) {
	write_user(user,"You have already requested an account.\n");
	return;
	}
if (word_count<2) {
	write_user(user,"Usage: accreq <an email address we can contact you>\n");
	return;
	}
/* Could check validity of email address I guess but its a waste of time.
   If they give a duff address they don't get an account, simple. ***/
sprintf(text,"ACCOUNT REQUEST from %s: %s.\n",user->name,word[1]);
write_syslog(text,1,TOSYS);
sprintf(text,"%s %s\n",user->name,word[1]);
write_syslog(text,1,TOACCOUNT);
sprintf(text,"~OLSYSTEM:~RS %s has made a request for an account: %s.\n",user->name,word[1]);
write_level(WIZ,1,text,NULL);
sprintf(text,"Account request logged : %s is %s\n",user->name,word[1]);
write_user(user,text);

user->accreq=1;
}


/*** Clear the review buffer ***/
revclr(user)
UR_OBJECT user;
{
char *name;

clear_revbuff(user->room); 
write_user(user,"Review buffer cleared.\n");
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s has cleared the review buffer.\n",name);
write_room_except(user->room,text,user);
}


#if 0

/*** Clone a user in another room 
create_clone(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;
int cnt;
char tvar[ROOM_NAME_LEN+1];


/* Check room 
if (word_count<2) rm=user->room;
else {
	inump(inpstr);
	if ((rm=get_room(inpstr,user))==NULL) return;
	}	
strcpy(tvar,rm->name);
delump(inpstr);


/* If room is private then nocando */
if (!has_room_access(user,rm)) {
	write_user(user,"That room is currently private, you cannot create a clone there.\n");  
	return;
	}
/* Count clones and see if user already has a copy there , no point having 
   2 in the same room */
cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->owner==user) {
		if (u->room==rm) {
			sprintf(text,"You already have a clone in the %s.\n",tvar);
			write_user(user,text);
			return;
			}	
		if (++cnt==max_clones) {
			write_user(user,"You already have the maximum number of clones allowed.\n");
			return;
			}
		}
	}
/* Create clone */
if ((u=create_user())==NULL) {		
	sprintf(text,"%s: Unable to create copy.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create user copy in clone().\n",0,TOSYS);
	return;
	}
u->type=CLONE_TYPE;
u->socket=user->socket;
u->room=rm;
u->owner=user;
strcpy(u->name,user->name);
strcpy(u->desc,"~BR(CLONE)");

if (rm==user->room)
	write_user(user,"~FB~OLYou whisper a haunting spell and a clone is created here.\n");
else {
	sprintf(text,"~FB~OLYou whisper a haunting spell and a clone is created in the %s.\n",rm->name);
	write_user(user,text);
	}
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~FB~OL%s whispers a haunting spell...\n",name);
write_room_except(user->room,text,user);
sprintf(text,"~FB~OLA clone of %s appears in a swirling magical mist!\n",user->name);
write_room_except(rm,text,user);
}

/* guai*/

/*** Destroy user clone ***/
destroy_clone(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u,u2;
RM_OBJECT rm;
char *name;
char tvar[ROOM_NAME_LEN+1];


/* Check room and user */
if (word_count<2) rm=user->room;
else {
	inump(inpstr);
	if ((rm=get_room(inpstr,user))==NULL) return;
	}
if (word_count>2) {
	if ((u2=get_user(word[2]))==NULL) {
		write_user(user,notloggedon);  return;
		}
	if (u2->level>=user->level) {
		write_user(user,"You cannot destroy the clone of a user of an equal or higher level.\n");
		return;
		}
	}
else u2=user;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==u2) {
		destruct_user(u);
		reset_access(rm);
		write_user(user,"~FM~OLYou whisper a sharp spell and the clone is destroyed.\n");
		if (user->vis) name=user->name; else name=invisname;
		sprintf(text,"~FM~OL%s whispers a sharp spell...\n",name);
		write_room_except(user->room,text,user);
		sprintf(text,"~FM~OLThe clone of %s shimmers and vanishes.\n",u2->name);
		write_room(rm,text);
		if (u2!=user) {
			sprintf(text,"~OLSYSTEM: ~FR%s has destroyed your clone in the %s.\n",user->name,rm->name);
			write_user(u2,text);
			}
		destructed=0;
		return;
		}
	}
if (u2==user) sprintf(text,"You do not have a clone in the %s.\n",rm->name);
else sprintf(text,"%s does not have a clone the %s.\n",u2->name,rm->name);
write_user(user,text);
}


/*** Show users own clones ***/
myclones(user)
UR_OBJECT user;
{
UR_OBJECT u;
int cnt;

cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type!=CLONE_TYPE || u->owner!=user) continue;
	if (++cnt==1) 
		write_user(user,"\n~BB*** Rooms you have clones in ***~RS\n\n");
	sprintf(text,"  %s\n",u->room);
	write_user(user,text);
	}
if (!cnt) write_user(user,"You have no clones.\n");
else {
	sprintf(text,"\nTotal of %d clones.\n\n",cnt);
	write_user(user,text);
	}
}


/*** Show all clones on the system ***/
allclones(user)
UR_OBJECT user;
{
UR_OBJECT u;
int cnt;

cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type!=CLONE_TYPE) continue;
	if (++cnt==1) {
		sprintf(text,"\n~BB*** Current clones %s ***~RS\n\n",long_date(1));
		write_user(user,text);
		}
	sprintf(text,"%-15s : %s\n",u->name,u->room);
	write_user(user,text);
	}
if (!cnt) write_user(user,"There are no clones on the system.\n");
else {
	sprintf(text,"\nTotal of %d clones.\n\n",cnt);
	write_user(user,text);
	}
}


/*** User swaps places with his own clone. All we do is swap the rooms the
	objects are in. ***/
clone_switch(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;

if (word_count<2) {
	write_user(user,"Usage: switch <room clone is in>\n");  return;
	}
if ((rm=get_room(word[1],user))==NULL) return;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) {
		write_user(user,"\n~FB~OLYou experience a strange sensation...\n");
		u->room=user->room;
		user->room=rm;
		sprintf(text,"The clone of %s comes alive!\n",u->name);
		write_room_except(user->room,text,user);
		sprintf(text,"%s turns into a clone!\n",u->name);
		write_room_except(u->room,text,u);
		look(user,0);
		return;
		}
	}
write_user(user,"You do not have a clone in that room.\n");
}


/*** Make a clone speak ***/
clone_say(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;
UR_OBJECT u;

if (user->muzzled) {
	write_user(user,"You are muzzled, your clone cannot speak.\n");
	return;
	}
if (word_count<3) {
	write_user(user,"Usage: csay <room clone is in> <message>\n");
	return;
	}
if ((rm=get_room(word[1],user))==NULL) return;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) {
		say(u,remove_first(inpstr));  return;
		}
	}
write_user(user,"You do not have a clone in that room.\n");
}


/*** Set what a clone will hear, either all speach , just bad language
	or nothing. ***/
clone_hear(user)
UR_OBJECT user;
{
RM_OBJECT rm;
UR_OBJECT u;

if (word_count<3  
    || (strcmp(word[2],"all") 
	    && strcmp(word[2],"swears") 
	    && strcmp(word[2],"nothing"))) {
	write_user(user,"Usage: chear <room clone is in> all/swears/nothing\n");
	return;
	}
if ((rm=get_room(word[1],user))==NULL) return;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) break;
	}
if (u==NULL) {
	write_user(user,"You do not have a clone in that room.\n");
	return;
	}
if (!strcmp(word[2],"all")) {
	u->clone_hear=CLONE_HEAR_ALL;
	write_user(user,"Clone will now hear everything.\n");
	return;
	}
if (!strcmp(word[2],"swears")) {
	u->clone_hear=CLONE_HEAR_SWEARS;
	write_user(user,"Clone will now only hear swearing.\n");
	return;
	}
u->clone_hear=CLONE_HEAR_NOTHING;
write_user(user,"Clone will now hear nothing.\n");
}

#endif

/*** Stat a remote system ***/
remote_stat(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
NL_OBJECT nl;
RM_OBJECT rm;

if (word_count<2) {
	write_user(user,"Usage: rstat <room service is linked to>\n");  return;
	}
inump(inpstr);

if ((rm=get_room(inpstr,user))==NULL) return;
if ((nl=rm->netlink)==NULL) {
	write_user(user,"That room is not linked to a service.\n");
	return;
	}
if (nl->stage!=2) {
	write_user(user,"Not (fully) connected to service.\n");
	return;
	}
if (nl->ver_major<=3 && nl->ver_minor<1) {
	write_user(user,"The NUTS version running that service does not support this facility.\n");
	return;
	}
sprintf(text,"RSTAT %s\n",user->name);
write_sock(nl->socket,text);
write_user(user,"Request sent.\n");
}


/*** Switch swearing ban on and off ***/
swban(user)
UR_OBJECT user;
{
if (!ban_swearing) {
	write_user(user,"Swearing ban ~FGON.\n");
	sprintf(text,"%s switched swearing ban ON.\n",user->name);
	write_syslog(text,1,TOSYS);
	ban_swearing=1;  return;
	}
write_user(user,"Swearing ban ~FROFF.\n");
sprintf(text,"%s switched swearing ban OFF.\n",user->name);
write_syslog(text,1,TOSYS);
ban_swearing=0;
}


/*** Do AFK ***/
afk(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (word_count>1) {
	if (!strcmp(word[1],"lock")) {
		if (user->type==REMOTE_TYPE) {
			/* This is because they might not have a local account and hence
			   they have no password to use. */
			write_user(user,"Sorry, due to software limitations remote users cannot use the lock option.\n");
			return;
			}
		inpstr=remove_first(inpstr);
		if (strlen(inpstr)>AFK_MESG_LEN) {
			write_user(user,"AFK message too long.\n");  return;
			}
		write_user(user,"You are now AFK with the session locked, enter your password to unlock it.\n");
		if (inpstr[0]) {
			strcpy(user->afk_mesg,inpstr);
			write_user(user,"AFK message set.\n");
			}
		user->afk=2;
		}
	else {
		if (strlen(inpstr)>AFK_MESG_LEN) {
			write_user(user,"AFK message too long.\n");  return;
			}
		write_user(user,"You are now AFK, press <return> to reset.\n");
		if (inpstr[0]) {
			strcpy(user->afk_mesg,inpstr);
			write_user(user,"AFK message set.\n");
			}
		user->afk=1;
		}
	}
else {
	write_user(user,"You are now AFK, press <return> to reset.\n");
	user->afk=1;
	}
if (user->vis) {
	if (user->afk_mesg[0]) 
		sprintf(text,"%s goes AFK: %s\n",user->name,user->afk_mesg);
	else sprintf(text,"%s goes AFK...\n",user->name);
	write_room_except(user->room,text,user);
	}
}


/*** Toggle user colour on and off ***/
toggle_colour(user)
UR_OBJECT user;
{
int col;

/* A hidden "feature" , notalot of practical use but lets see if any users
   stumble across it :) */
if (user->command_mode && user->ignall && user->charmode_echo) {
	for(col=1;col<NUM_COLS;++col) {
		sprintf(text,"%s: ~%sNUTS 3 VIDEO TEST~RS\n",colcom[col],colcom[col]);
		write_user(user,text);
		}
	return;
	}
if (user->colour) {
	write_user(user,"Colour ~FROFF.\n");  
	user->colour=0;
	}
else {
	user->colour=1;  
	write_user(user,"Colour ~FGON.\n");
	}
if (user->room==NULL) prompt(user);
}



toggle_igntell(user)
UR_OBJECT user;
{
if (user->igntell) {
	write_user(user,"You are no longer ignoring tells and private emotes.\n");  
	user->igntell=0;
	return;
	}
write_user(user,"You are now ignoring tells and private emotes.\n");
user->igntell=1;
}


suicide(user)
UR_OBJECT user;
{
if (word_count<2) {
	write_user(user,"Usage: suicide <your password>\n");  return;
	}
if (strcmp((char *)crypt(word[1],"NU"),user->pass)) {
	write_user(user,"Password incorrect.\n");  return;
	}
write_user(user,"\n\07~FR~OL~LI*** WARNING - This will delete your account! ***\n\nAre you sure about this (y/n)? ");
user->misc_op=6;  
no_prompt=1;
}


/*** Delete a user ***/
delete_user(user,this_user)
UR_OBJECT user;
int this_user;
{
UR_OBJECT u;
char filename[80],name[USER_NAME_LEN+1];

if (this_user) {
	/* User structure gets destructed in disconnect_user(), need to keep a
	   copy of the name */
	strcpy(name,user->name); 
	write_user(user,"\n~FR~LI~OLACCOUNT DELETED!\n");
	sprintf(text,"~OL~LI%s commits suicide!\n",user->name);
	write_room_except(user->room,text,user);
	sprintf(text,"%s SUICIDED.\n",name);
	write_syslog(text,1,TOSYS);
	disconnect_user(user);
	sprintf(filename,"%s/%s.D",USERFILES,name);
	unlink(filename);
	sprintf(filename,"%s/%s.M",USERFILES,name);
	unlink(filename);
	sprintf(filename,"%s/%s.P",USERFILES,name);
	unlink(filename);	
	sprintf(filename,"%s/%s.L",USERFILES,name);
	unlink(filename);	
	remove_from_user_list(name);
	return;
	}
if (word_count<2) {
	write_user(user,"Usage: delete <user>\n");  return;
	}
word[1][0]=toupper(word[1][0]);
if (!strcmp(word[1],user->name)) {
	write_user(user,"Trying to delete yourself is the eleventh sign of madness.\n");
	return;
	}
if (get_user(word[1])!=NULL) {
	/* Safety measure just in case. Will have to .kill them first */
	write_user(user,"You cannot delete a user who is currently logged on.\n");
	return;
	}
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user object in delete_user().\n",0,TOSYS);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot delete a user of an equal or higher level than yourself.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
destruct_user(u);
destructed=0;
sprintf(filename,"%s/%s.D",USERFILES,word[1]);
unlink(filename);
sprintf(filename,"%s/%s.M",USERFILES,word[1]);
unlink(filename);
sprintf(filename,"%s/%s.P",USERFILES,word[1]);
unlink(filename);
sprintf(filename,"%s/%s.L",USERFILES,word[1]);
unlink(filename);	

remove_from_user_list(word[1]);
sprintf(text,"\07~FR~OL~LIUser %s deleted!\n",word[1]);
write_user(user,text);
sprintf(text,"%s DELETED %s.\n",user->name,word[1]);
write_syslog(text,1,TOSYS);
}

/*** Shutdown talker interface func. Countdown time is entered in seconds so
	we can specify less than a minute till reboot. ***/
shutdown_com(user)
UR_OBJECT user;
{
if (rs_which==1) {
	write_user(user,"The reboot countdown is currently active, you must cancel it first.\n");
	return;
	}
if (!strcmp(word[1],"cancel")) {
	if (!rs_countdown || rs_which!=0) {
		write_user(user,"The shutdown countdown is not currently active.\n");
		return;
		}
	if (rs_countdown && !rs_which && rs_user==NULL) {
		write_user(user,"Someone else is currently setting the shutdown countdown.\n");
		return;
		}
	write_room(NULL,"~OLSYSTEM:~RS~FG Shutdown cancelled.\n");
	sprintf(text,"%s cancelled the shutdown countdown.\n",user->name);
	write_syslog(text,1,TOSYS);
	rs_countdown=0;
	rs_announce=0;
	rs_which=-1;
	rs_user=NULL;
	return;
	}
if (word_count>1 && !isnumber(word[1])) {
	write_user(user,"Usage: shutdown [<secs>/cancel]\n");  return;
	}
if (rs_countdown && !rs_which) {
	write_user(user,"The shutdown countdown is currently active, you must cancel it first.\n");
	return;
	}
if (word_count<2) {
	rs_countdown=0;  
	rs_announce=0;
	rs_which=-1; 
	rs_user=NULL;
	}
else {
	rs_countdown=atoi(word[1]);
	rs_which=0;
	}
write_user(user,"\n\07~FR~OL~LI*** WARNING - This will shutdown the talker! ***\n\nAre you sure about this (y/n)? ");
user->misc_op=1;  
no_prompt=1;  
}


/*** Reboot talker interface func. ***/
reboot_com(user)
UR_OBJECT user;
{
if (!rs_which) {
	write_user(user,"The shutdown countdown is currently active, you must cancel it first.\n");
	return;
	}
if (!strcmp(word[1],"cancel")) {
	if (!rs_countdown) {
		write_user(user,"The reboot countdown is not currently active.\n");
		return;
		}
	if (rs_countdown && rs_user==NULL) {
		write_user(user,"Someone else is currently setting the reboot countdown.\n");
		return;
		}
	write_room(NULL,"~OLSYSTEM:~RS~FG Reboot cancelled.\n");
	sprintf(text,"%s cancelled the reboot countdown.\n",user->name);
	write_syslog(text,1,TOSYS);
	rs_countdown=0;
	rs_announce=0;
	rs_which=-1;
	rs_user=NULL;
	return;
	}
if (word_count>1 && !isnumber(word[1])) {
	write_user(user,"Usage: reboot [<secs>/cancel]\n");  return;
	}
if (rs_countdown) {
	write_user(user,"The reboot countdown is currently active, you must cancel it first.\n");
	return;
	}
if (word_count<2) {
	rs_countdown=0;  
	rs_announce=0;
	rs_which=-1; 
	rs_user=NULL;
	}
else {
	rs_countdown=atoi(word[1]);
	rs_which=1;
	}
write_user(user,"\n\07~FY~OL~LI*** WARNING - This will reboot the talker! ***\n\nAre you sure about this (y/n)? ");
user->misc_op=7;  
no_prompt=1;  
}



/*** Show recorded tells and pemotes ***/
revtell(user)
UR_OBJECT user;
{
int i,cnt,line;

cnt=0;
for(i=0;i<REVTELL_LINES;++i) {
	line=(user->revline+i)%REVTELL_LINES;
	if (user->revbuff[line][0]) {
		cnt++;
		if (cnt==1) write_user(user,"\n~BB~FG*** Your revtell buffer ***~RS\n\n");
		write_user(user,user->revbuff[line]); 
		}
	}
if (!cnt) write_user(user,"Revtell buffer is empty.\n");
else write_user(user,"\n~BB~FG*** End ***~RS\n\n");
}


to(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;

if (user->muzzled) {
        write_user(user,"You are muzzled, you cannot speak.\n");  return;
        }
if (word_count<3) {
        write_user(user,"Say who what?\n");  return;
        }

if (!(u=get_user(word[1]))) {
        write_user(user,notloggedon);  return;
        }

if (ban_swearing && contains_swearing(inpstr)) {
        write_user(user,noswearing);  return;
        }

        if (u->login
            || u->room==NULL
            || (u->room!=user->room && user->room!=NULL)) {
                write_user(user,"he isn't here at the moment\n");
                return;
        }
        if (u->ignall && !force_listen) {
                write_user(user,"he is ignoring everyone at the moment\n");
                return;
        }

        if (u->afk) {
                write_user(user,"he is AFK at the moment\n");
                return;
        }



inpstr=remove_first(inpstr);
write_to_one(user,inpstr,u);
}


write_to_one(user,inpstr,u)
UR_OBJECT user,u;
char *inpstr;
{
char type[10],type2[4],*name,*name2;

switch(inpstr[strlen(inpstr)-1]) {
     case '?': strcpy(type,"ask");  break;
     case '!': strcpy(type,"exclaim");  break;
     default : strcpy(type,"say");
     }

if (u==user) {
	switch (user->sex) {
		case 'M' : strcpy(type2,"him"); break;
		case 'F' : strcpy(type2,"her"); break;
		default : strcpy(type2,"it"); break;
		}
 
	sprintf(text,"You %s to yourself: %s\n",type,inpstr);
	write_user(user,text);
	if (user->vis) name=user->name; else name=invisname;
	sprintf(text,"%s %ss to %sself: %s\n",name,type,type2,inpstr);
	write_room_except(user->room,text,user);
	record(user->room,text);
	return;
	}

if (u->vis) name=u->name; else name=invisname;
sprintf(text,"You %s to %s: %s\n",type,name,inpstr);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s %ss to ~OLyou~RS: %s\n",name,type,inpstr);
write_user(u,text);

if (u->vis) name2=u->name; else name2=invisname;
sprintf(text,"%s %ss to %s: %s\n",name,type,name2,inpstr);
write_room_except2(user->room,text,user,u);
record(user->room,text);
}

/*** Read a document ***/
document(user)
UR_OBJECT user;
{
char filename[80],*name;
int ret;
char fil[30];

if (!strstr(user->room->maptype,"bib")) {
        write_user(user,"You can't use this command here\n");
        return;
        }

if (word_count<2) strcpy(fil,"index");
else {
        if (strlen(word[1])>29) {
                write_user(user,"filename too long...\n");
                return;
                }
        strcpy(fil,word[1]);
        }

sprintf(filename,"%s/%s%s.doc",FILEDIR,fil,user->room->maptype);

if (!(ret=more(user,user->socket,filename))) {
        write_user(user,"This document doesn't exists\n\n");
        return;
        }
else if (ret==1) user->misc_op=2;
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s reads the document %s.\n",name,fil);
write_room_except(user->room,text,user);
}


room_opt(user,inpstr)
UR_OBJECT user;
char *inpstr;
{

if (word_count<2) {	
	if(user->level>=ADVANCED) {
		write_user(user,"usage: .room [open/link/slink/map/desc/status/delete] <other info...>\n");
	        sprintf(text,"\n"); 
		return;
		}
	if(user->level>MAGHETTO && user->level<ADVANCED) {
		write_user(user,"usage: .room [open/link/slink/map/desc/status] <other info...>\n");
	        sprintf(text,"\n"); 
		return;
		}
	if (user->level==MAGHETTO) {
		write_user(user,"usage: .room [status/desc] <other info>\n");
		return;
		}
	if (user->level==HELPER) {
		write_user(user,"usage: .room [status] <room-name>\n");
		return;
		}
	}

if (!(strcmp("open",word[1]))) {
	if (user->level<WIZ) { write_user(user,"Invalid option\n"); return; }
	inpstr=remove_first(inpstr);
	while(*inpstr==' ') inpstr++;
	if (*inpstr=='\0') { write_user(user,"specify room name, please\n"); return; }
	get_end(inpstr);
	room_open(user,inpstr);
	return;
	}


if (!(strcmp("link",word[1]))) {
	if (user->level<WIZ) { write_user(user,"Invalid option\n"); return; }
	inpstr=remove_first(inpstr);
	while(*inpstr==' ') inpstr++;
	if (*inpstr=='\0') {	write_user(user,"specify room name, please\n"); return; }
	get_end(inpstr);
	room_link(user,inpstr);	
	return;
	}


if (!(strcmp("slink",word[1]))) {
	if (user->level<WIZ) { write_user(user,"Invalid option\n"); return; }
	inpstr=remove_first(inpstr);
	while(*inpstr==' ') inpstr++;
	if (*inpstr=='\0') {	write_user(user,"specify room name, please\n"); return; }
	get_end(inpstr);
	room_slink(user,inpstr);	
	return;
	}


if (!(strcmp("desc",word[1]))) {
	if (user->level<MAGHETTO) { write_user(user,"Invalid option\n"); return; }
		if (user->type==REMOTE_TYPE) {
			write_user(user,"sorry, due to software limitation you can't use this option\n");
			return;
			}
		room_desc(user,0);
		return;
		}


if (!(strcmp("map",word[1]))) {
	if (user->level<WIZ) { write_user(user,"Invalid option\n"); return; }
		room_map(user);
		return;
	}
 
if (!(strcmp("status",word[1]))) {
	room_status(user,inpstr);
	return;
	}


if (!(strcmp("delete",word[1]))) {
	if (user->level<ADVANCED) { write_user(user,"Invalid option\n"); return; }
	inpstr=remove_first(inpstr);
	while(*inpstr==' ') inpstr++;
	if (*inpstr=='\0') { write_user(user,"specify room name, please\n"); return; }
	get_end(inpstr);
	room_delete(user,inpstr);
	return;
	}

if (!(strcmp("unlink",word[1]))) {
	if (user->level<WIZ) { write_user(user,"Invalid option\n"); return; }
	inpstr=remove_first(inpstr);
	while(*inpstr==' ') inpstr++;
	if (*inpstr=='\0') {	write_user(user,"specify room name, please\n"); return; }
	get_end(inpstr);
	room_unlink(user,inpstr);	
	return;
	}

write_user(user,"Invalid option\n");
}


room_open(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT room;
char tvar1[ROOM_NAME_LEN+1],tvar2[ROOM_NAME_LEN+1];

if (strstr(inpstr,"_")) {
	write_user(user,"the char _ is not admitted\n");
	return;
	}

inump(inpstr);

if (strlen(inpstr)>ROOM_NAME_LEN) {
	write_user(user,"the room name is too long\n");
	return;
	}

for (room=room_first; room!=NULL; room=room->next) {
	strcpy(tvar1,room->name);
	strcpy(tvar2,inpstr);
	strtolower(tvar1);
	strtolower(tvar2);
	if (!(strcmp(tvar1,tvar2))) {
		write_user(user,"this room name is already used.\n");
		return;
		}
	}

if (!(strcmp(inpstr,"null"))) {
	write_user(user,"The name null is not valid\n");
	return;
	}

if ((room=create_room())==NULL) {
	sprintf(text,"%s: unable to create room object.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create room object in room_open()\n",1,TOSYS);
	return;
	}


strcpy(room->name,inpstr);
strcpy(room->label,inpstr);

room->access=FIXED_PUBLIC;
strcpy(room->maptype,"a");

sprintf(text,"%s successfully created room %s \n",user->name,room->name);

write_syslog(text,1,TOSYS);
write_syslog(text,1,TOROOM);

strcpy(inpstr,room->name);
delump(inpstr);

move_user(user,room,1);

sprintf(text,"~OL~FWSYSTEM: ~FGroom %s successfully created\n",inpstr);

write_user(user,text);
room_save(user);
}


room_link(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT room,rm;
int flag=0,last_rm=MAX_LINKS,last_room=MAX_LINKS,i;
char tvar1[ROOM_NAME_LEN+1],tvar2[ROOM_NAME_LEN+1];

rm=user->room;
inump(inpstr);

if ((room=get_room(inpstr,user))==NULL) return;

if (rm==room) {
        write_user(user,"You can't join a room to itself\n");
        return;
        }

for(i=MAX_LINKS-1;i>=0;i--) {
	if (rm->link[i]==room) flag+=1; 
	if (room->link[i]==rm) flag+=2;
	if (rm->link[i]==NULL) last_rm=i;
	if (room->link[i]==NULL) last_room=i;
	}


if (flag==3) {
	write_user(user,"These rooms are already linked\n");
	return;
	}

if (last_rm==MAX_LINKS) {
	strcpy(tvar1,rm->name);
	delump(tvar1);
	sprintf(text,"Sorry, no more links available for %s\n",tvar1);
	write_user(user,text);
	return;
	}


if (last_room==MAX_LINKS) {
	strcpy(tvar1,room->name);
	delump(tvar1);
	sprintf(text,"Sorry, no more links available for %s\n",tvar1);
	write_user(user,text);
	return;
	}

switch (flag) {
	case 0:	rm->link[last_rm]=room;
		room->link[last_room]=rm;
		strcpy(tvar1,rm->name);
		strcpy(tvar2,room->name);
		delump(tvar1);
		delump(tvar2);
		sprintf(text,"~OL~FWSYSTEM: ~FGroom %s linked to room %s and vice-versa\n",tvar1,tvar2);
		write_user(user,text);
		sprintf(text,"%s has linked %s to %s and vice-versa\n",user->name,rm->name,room->name);
		write_syslog(text,1,TOROOM);
		write_syslog(text,1,TOSYS);
		break;

	case 1: room->link[last_room]=rm;
                strcpy(tvar1,rm->name);
                strcpy(tvar2,room->name);
                delump(tvar1);
                delump(tvar2);
                sprintf(text,"~OL~FWSYSTEM: ~FGroom %s already linked to room %s. Created double way link\n",tvar1,tvar2);
                write_user(user,text);
                sprintf(text,"%s has linked %s to %s and vice-versa\n",user->name,rm->name,room->name); 
		write_syslog(text,1,TOROOM);
                write_syslog(text,1,TOSYS);
                break;

	case 2: rm->link[last_rm]=room;
                strcpy(tvar1,rm->name);
                strcpy(tvar2,room->name);
                delump(tvar1);
                delump(tvar2);
                sprintf(text,"~OL~FWSYSTEM: ~FGroom %s already linked to room %s. Created double way link\n",tvar2,tvar1);
                write_user(user,text);
                sprintf(text,"%s has linked %s to %s and vice-versa\n",user->name,rm->name,room->name); 
		write_syslog(text,1,TOROOM);
                write_syslog(text,1,TOSYS);
                break;
	}

room_save(user);
}


room_slink(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT room,rm;
char tvar1[ROOM_NAME_LEN+1],tvar2[ROOM_NAME_LEN+1];
int i;


rm=user->room;
inump(inpstr);

if ((room=get_room(inpstr,user))==NULL) return;

if (rm==room) {
        write_user(user,"You can't join a room to itself\n");
        return;
        }

/* cerca il primo link disponibile */

for(i=0;i<MAX_LINKS;++i) {
        if (rm->link[i]==room) {
                write_user(user,"The room is already linked...\n");
                return;
                }
        if (rm->link[i]==NULL) break;
        }


if (i==MAX_LINKS) {
        write_user(user,"Sorry... no more links available\n");
        return;
        }

rm->link[i]=room;
strcpy(tvar1,room->name);
strcpy(tvar2,rm->name);
delump(tvar1);
delump(tvar2);
sprintf(text,"~OL~FWSYSTEM : ~FGroom %s slinked to room %s\n",tvar2,tvar1);
write_user(user,text);
sprintf(text,"%s has slinked room %s to room %s\n",user->name,tvar2,tvar1);
write_syslog(text,1,TOROOM);
write_syslog(text,1,TOSYS);
room_save(user);
}


room_desc(user,done_editing)
UR_OBJECT user;
int done_editing;
{
FILE *fp;
char *c,filename[80];
int i;
RM_OBJECT rm;

if (!done_editing) {
        write_user(user,"\n~BB*** Writing room description ***\n\n");
        user->misc_op=8;
        editor(user,NULL,ROOM_LINES);
        return;
        }

rm=user->room;

sprintf(filename,"%s/%s.R",DATAFILES,rm->name);
if (!(fp=fopen(filename,"w"))) {
        write_user(user,"~OL~FWERROR : ~FRcouldn't save room description.\n");
        sprintf("ERROR: Couldn't open file %s to write in room_desc().\n",filename);
        write_syslog(text,1,TOROOM);
        write_syslog(text,1,TOSYS);
        return;
        }
c=user->malloc_start;
while(c!=user->malloc_end) putc(*c++,fp);
fclose(fp);
write_user(user,"~OL~FWSYSTEM : ~FGroom description stored.\n");
sprintf(text,"%s has changed the %s desc\n",user->name, rm->name);
write_syslog(text,1,TOROOM);
write_syslog(text,1,TOSYS);

}


room_map(user)
UR_OBJECT user;
{
RM_OBJECT rm;
char filename[80];
int ret;

rm=user->room;

if(word_count<3) {
        write_user(user,"specify map type, please. Type .room map reserved for a list\n");
        return;
        }

if(!(strcmp("reserved",word[2]))) {
        sprintf(filename,"%s/%s",DATAFILES,RESMAPTYPE);
        if (!(ret=more(user,user->socket,filename)))
                write_user(user,"There are no reserved map types.\n");
        else if (ret==1) user->misc_op=2;
        return;
        }

if (strlen(word[2])>MAPTYPE_LEN) {
        write_user(user,"The map type label is too long\n");
        return;
        }

strcpy(rm->maptype,word[2]);

sprintf(text,"~FW~OLSYSTEM : ~FGthe room map type is set to %s\n",rm->maptype);
write_user(user,text);
sprintf(text,"%s has changed the %s maptype to %s\n",user->name,rm->name,rm->maptype);
write_syslog(text,1,TOSYS);
write_syslog(text,1,TOROOM);
room_save(user);
}

room_status(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;
char tvar[ROOM_NAME_LEN+1];
char *pun;
int i,exits;
char temp[ROOM_NAME_LEN+1];


if (word_count<3) rm=user->room;
else {
        inpstr=remove_first(inpstr);

        while(*inpstr==' ') inpstr++;
	get_end(inpstr);
        inump(inpstr);
        if ((rm=get_room(inpstr,user))==NULL) return;
        }

strcpy(tvar,rm->name);
delump(tvar);

sprintf(text,"*** room status : %s ***\n",tvar);
write_user(user,text);
sprintf(text,"Topic: %s\n",rm->topic);
write_user(user,text);
strcpy(text,"Access: ");
switch(rm->access) {
        case PUBLIC:  strcat(text,"set to ~FGPUBLIC~RS\n");  break;
        case PRIVATE: strcat(text,"set to ~FRPRIVATE~RS\n");  break;
        case FIXED_PUBLIC:  strcat(text,"~FRfixed~RS to ~FGPUBLIC~RS\n"); break;
        case FIXED_PRIVATE: strcat(text,"~FRfixed~RS to ~FRPRIVATE~RS\n"); break;
        }
write_user(user,text);

strcpy(text,"linked to: ");

exits=0;

for(i=0;i<MAX_LINKS;++i) {
        if (rm->link[i]==NULL) break;
        if (rm->link[i]->access & PRIVATE)
                sprintf(temp,"  ~FR%s",rm->link[i]->name);
        else sprintf(temp,"  ~FG%s",rm->link[i]->name);
        strcat(text,temp);
        ++exits;
        }
if (rm->netlink!=NULL && rm->netlink->stage==UP) {
        if (rm->netlink->allow==IN) sprintf(temp,"~FR%s*",rm->netlink->service);
        else sprintf(temp,"  ~FG%s*",rm->netlink->service);
        strcat(text,temp);
        }
else if (!exits) strcat(text,"none");
strcat(text,"\n");

delump(text);
write_user(user,text);

sprintf(text,"Maptype : %s\n",rm->maptype);
write_user(user,text);
return;
}


room_save(user)
UR_OBJECT user;
{
char filename[80];
FILE *fp;
RM_OBJECT room;
int i;
char *pun;

strcpy(filename,"temproomconfig");

if (!(fp=fopen(filename,"w"))) {
        write_user(user,"~OL~FWERROR : ~FRcan't open room temp file");
        write_syslog("can't open room temp file in room_save()",1,TOSYS);
        return;
	}

for (room=room_first; room!=NULL; room=room->next) {
	sprintf(text,"%s ",room->name);
	for(i=0;i<MAX_LINKS;++i) {
		if (room->link[i]==NULL) break;
		strcat(text,room->link[i]->name);
		strcat(text,",");
		}
	if (!i) strcat(text,"null,");
	pun=text;
	while (*pun!='\0') pun++;
	*(pun-1)=' ';
	strcat(text,room->maptype);
	switch(room->access) {
		case PUBLIC:
		case PRIVATE: strcat(text," BOTH "); break;
		case FIXED_PRIVATE: strcat(text," PRIV "); break;
		case FIXED_PUBLIC: strcat(text," PUB "); break;
		default: strcat(text," BOTH ");
		}
	if (room->netlink!=NULL && !room->inlink) {
		strcat(text,"CONNECT ");
		strcat(text,room->netlink->service);
		}
	if (room->inlink==1) strcat(text,"ACCEPT");
	strcat(text,"\n");
	fputs(text,fp);
	}

fclose(fp);

sprintf(text,"%s/%s",DATAFILES,ROOMCONFIG);
rename(filename,text);

}


room_delete(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT room,victim;
UR_OBJECT u;
int i,j;
char tvar[ROOM_NAME_LEN+1];

inump(inpstr);

if ((victim=get_room(inpstr,user))==NULL) return;


for (u=user_first;u!=NULL;u=u->next) {
	if (u->room==victim) {
		write_user(user,"The room contains user(s)\n");
		return;
		}
	}


for (room=room_first;room!=NULL;room=room->next) {
	for(i=0;i<MAX_LINKS;++i) {
		if (room->link[i]==victim) {
			for (j=i;j<MAX_LINKS-1;++j) room->link[j]=room->link[j+1];
			room->link[MAX_LINKS-1]=NULL;
			}
		}
	}



sprintf(text,"%s has deleted %s\n",user->name,victim->name);
write_syslog(text,1,TOSYS);
write_syslog(text,1,TOROOM);

strcpy(tvar,victim->name);
delump(tvar);

destruct_room(victim);

sprintf(text,"~OLSYSTEM : ~FGroom %s deleted\n",tvar);
write_user(user,text);
room_save(user);
}

room_unlink(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT room,victim;
UR_OBJECT u;
int i,j,flag=0;
char tvar[ROOM_NAME_LEN+1];

room=user->room;

inump(inpstr);

if ((victim=get_room(inpstr,user))==NULL) return;

if (victim==room) {
	write_user(user,"what???\n");
	return;
	}


for(i=0;i<MAX_LINKS;++i) {
	if (room->link[i]==victim) {
		for (j=i;j<MAX_LINKS-1;++j) room->link[j]=room->link[j+1];
		room->link[MAX_LINKS-1]=NULL;
		flag=1;
		}
	if (victim->link[i]==room) {
		for (j=i;j<MAX_LINKS-1;++j) victim->link[j]=victim->link[j+1];
		victim->link[MAX_LINKS-1]=NULL;
		flag=1;
		}
	}


strcpy(tvar,victim->name);
delump(tvar);

if (!flag) {
	sprintf(text,"The room %s is not adjoined to here\n",tvar);
	write_user(user,text);
	return;
	}


sprintf(text,"%s has unlinked %s and %s\n",user->name,room->name,victim->name);
write_syslog(text,1,TOSYS);
write_syslog(text,1,TOROOM);

sprintf(text,"~OLSYSTEM : ~FGroom %s unlinked and vice-versa\n",tvar);
write_user(user,text);
room_save(user);
}


path(user)
UR_OBJECT user;
{
int value;

if (user->path!=0) {
	write_user(user,"You can't change your path again\n");
	return;
	}

if (word_count<2) {
	write_user(user,"usage: .path <path number>\n");
	return;
	}

value= atoi(word[1]);

if (value>MAX_LEVEL_TYPE || value<0) {
	write_user(user,"invalid value\n");
	return;
	}

user->path=value;

sprintf(text,"Now your level name is : ~OL%s\n",level_name[user->path][user->level]);

write_user(user,text);

}

level_list(user)
UR_OBJECT user;
{
int value,i;


if (word_count<2) {
	write_user(user,"usage: .level <path number>\n\n");
	write_user(user,"Tipi di path disponibili:\n");
	for (i=0; i<=MAX_LEVEL_TYPE; i++) {
		sprintf(text,"%d. %s\n",i,level_type[i]);
		write_user(user,text);
		}
	return;
	}

value= atoi(word[1]);


if (value>MAX_LEVEL_TYPE || value<0) {
	write_user(user,"invalid value\n");
	return;
	}

i=0;

sprintf(text,"~FT(%s)\n",level_type[value]);
write_user(user,text);

while(level_name[value][i][0]!='*') {
		sprintf(text,"%d. %s\n",i,level_name[value][i]);
		write_user(user,text);
		++i;
		}
}

hulk(user)
UR_OBJECT user;
{
UR_OBJECT victim;
RM_OBJECT rm;
char *name;
int i;
char filename[80];
char vicname[USER_NAME_LEN+1];


if (word_count<2) {
        write_user(user,"Usage: hulk <user>\n");  return;
        }
if (!(victim=get_user(word[1]))) {
        write_user(user,notloggedon);  return;
        }
if (user==victim) {
        write_user(user,"Trying to Hulk yourself is the n-th sign of madness\n");
        return;
        }

if (victim->level>=user->level) {
        write_user(user,"You cannot hulk a user of equal or higher level than yourself\nD");
        sprintf(text,"%s tried to hulk you!\n",user->name);
        write_user(victim,text);
        return;
        }


if (victim->type==REMOTE_TYPE) {
	write_user(user,"You can't hulk a remote user... use .kill <user>\n");
	return;
	}


rm=victim->room;

strcpy(vicname,victim->name);

sprintf(text,"%s HULKED %s.\n",user->name,vicname);
write_syslog(text,1,TOSYS);
write_user(user,"~FG~OLSenti una forza misteriosa crescere dentro di te...\n");
write_user(user,"~FG~OLI tuoi vestiti si strappano...\n");
write_user(user,"~FG~OLLa tua pelle diventa verde...\n");
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"~FG~OL%s sente una forza misteriosa crescere dentro di se'...\n",name);
write_room_except(user->room,text,user);
write_room_except(user->room,"~OL~FGI suoi vestiti si strappano...\n",user);
write_room_except(user->room,"~OL~FGLa sua pelle diventa verde...\n",user);
if (user->room!=rm) {
        sprintf(text,"~OL~FGHulk-%s parte per una missione spietata\n",user->name);
        write_room_except(user->room,text,user);
        }


sprintf(text,"~OL~FGHulk-%s ti si para davanti, sfoderando i suoi poderosi muscoli\n",user->name);
write_user(victim,text);

sprintf(text,"~OL~FGHulk-%s si para davanti a %s, sfoderando i suoi poderosi muscoli\n",user->name,vicname);
write_room_except2(rm,text,victim,user);
sprintf(text,"~OL~FGTi pari davanti a %s, sfoderando i tuoi poderosi muscoli\n",vicname);
write_user(user,text);
for (i=0 ;i<5; i++) {
        sprintf(text,"~OL~FGHulk-%s says to You: BRUTTO CATTIVO!!!!\n",user->name);
        write_user(victim,text);
        sprintf(text,"~OL~FGHulk-%s says to %s: BRUTTO CATTIVO!!!!\n",user->name,vicname);
        write_room_except2(rm,text,victim,user);
        sprintf(text,"~OL~FGYou say to %s: BRUTTO CATTIVO!!!!\n",vicname);
        write_user(user,text);
        }

sprintf(text,"~OL~FGHulk-%s ti tira due malrovesci da paura...\n",user->name);
write_user(victim,text);


sprintf(text,"~OL~FGHulk-%s tira due malrovesci da paura a %s\n",user->name,vicname);
write_room_except2(rm,text,victim,user);

sprintf(text,"~OL~FGTiri due malrovesci da paura a %s\n",vicname);

disconnect_user(victim);
write_room(NULL,"~OL~FGSenti un urlo lacerante, di atroce sofferenza, e poi... il silenzio...\n");


if (user->room!=rm) {
        sprintf(text,"~OL~FGHulk-%s rientra con le mani insanguinate\n",user->name);
        write_room_except(user->room,text,user);
        }
sprintf(text,"~OL~FGHulk-%s torna normale\n",user->name);
write_room_except(user->room,text,user);
write_user(user,"~OL~FGRitorni normale\n");
sprintf(text,"~OL~FG%s ha compiuto la sua missione...\n",user->name);
write_room_except(rm,text,user);

sprintf(filename,"%s/%s.D",USERFILES,vicname);
unlink(filename);
sprintf(filename,"%s/%s.M",USERFILES,vicname);
unlink(filename);
sprintf(filename,"%s/%s.P",USERFILES,vicname);
unlink(filename);
sprintf(filename,"%s/%s.L",USERFILES,vicname);
unlink(filename);	

remove_from_user_list(vicname);

}

undo(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_user(user,"usage: .undo <user name>\n");
	return;
	}

if (!(u=get_user(word[1]))) {
        write_user(user,notloggedon);  return;
        }

sprintf(text,"%s used .undo on user %s\n",user->name,u->name);
write_syslog(text,1,TOSYS);


u->accreq=0;

sprintf(text,"You used .undo on user %s\n",u->name);

write_user(user,text);


}

aspect(user,done_editing)
UR_OBJECT user;
int done_editing;
{
FILE *fp;
char *c,filename[80];

if (!done_editing) {
        write_user(user,"\n~BB*** Writing your aspect ***~RS\n\n");
        user->misc_op=9;
        editor(user,NULL,ASPECT_LINES);
        return;
        }

sprintf(filename,"%s/%s.L",USERFILES,user->name);
if (!(fp=fopen(filename,"w"))) {
        sprintf(text,"%s: couldn't save your aspect.\n",syserror);
        write_user(user,text);
        sprintf("ERROR: Couldn't open file %s to write in aspect().\n",filename);
        write_syslog(text,0,TOSYS);
        return;
        }
c=user->malloc_start;
while(c!=user->malloc_end) putc(*c++,fp);
fclose(fp);
write_user(user,"Aspect stored.\n");
}
 
join(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
char tvar[ROOM_NAME_LEN+1];

if (word_count<2) {
	write_user(user,"usage: .join <user>\n");
	return;
	}

if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);
	return;
	}

if (u==user) {
	write_user(user,"you can't join yourself\n");
	return;
	}

rm=u->room;

if (rm==NULL) {
	write_user(user,"you can't join an offsite user\n");
	return;
	}

if (rm==user->room) {
	write_user(user,"you are already in that room\n");
	return;
	}

if (user->level<USER) {
	strcpy(tvar,rm->name);
	delump(tvar);
	sprintf(text,"%s is in the %s\n",u->name,tvar);
	write_user(user,text);
	return;
	}

move_user(user,rm,1);
} 

run_macro(user,global)
UR_OBJECT user;
int global;
{
UR_OBJECT u;
int retval;
char filename[80],buff[OUT_BUFF_SIZE+1],*pun,*punb,*name,*macroname;
FILE *fp;

/* check delle condizioni... */
if (user->muzzled) {
	if(user->misc_op) sprintf(text,"You have been muzzled, macro stopped\n");
		else sprintf(text,"You are muzzled, you can't run macros\n");
	write_user(user,text);
	end_macro(user);
	return;
	}

if (!(u=get_user(user->macro))) {
	write_user(user,"The user has logged out, macro stopped\n");
	end_macro(user);	
	return;
	}

if(user->room!=u->room) {
	if(user->misc_op) sprintf(text,"The user has gone in another room, macro stopped\n");
		else sprintf(text,"The user is in another room\n");
	write_user(user,text);
	end_macro(user);
	return;
	}

if(u->afk) {
	if(user->misc_op) sprintf(text,"The user is AFK\n");
		else sprintf(text,"The user is AFK, macro stopped\n");
	write_user(user,text);
	end_macro(user);
	return;
	}

if (u->ignall) {
	if (u->malloc_start!=NULL) 
		sprintf(text,"%s is using the editor at the moment\n",u->name);
	else sprintf(text,"%s is ignoring everyone\n",u->name);
	write_user(user,text);
	end_macro(user);
	return;
	}

/* fine check... dimentico qualcosa??? */

if (global) sprintf(filename,"%s/%s",MACRODIR,GLOBALMACRO);
else sprintf(filename,"%s/%s.macro",MACRODIR,user->name);

if (!(fp=fopen(filename,"r"))) return;

text[0]='\0';
buff[0]='\0';

fseek(fp,user->filepos,0);

fgets(text,sizeof(text)-1,fp);

user->filepos+=strlen(text);

if (user->vis) name=user->name; else name=invisname;
macroname=user->macro;

/* controlla che non abbia preso su delle righe di controllo,
   quelle che iniziano con @                                */

while(text[0]=='@') {
	fgets(text,sizeof(text)-1,fp);
	user->filepos+=strlen(text);
	}

/**** ora hai la linea... interpretala ********/
/* copia la stringa in buff, sostituendo a %1 e %2 i nomi */
while (text[0]!='#') {
	pun=text;
	punb=buff;
	while (*pun!='\0') {
		if (&(buff[OUT_BUFF_SIZE])-punb<USER_NAME_LEN) break;
		if (*pun=='%') {
			if (*(pun+1)=='1') {
				*punb='\0';
				strcat(buff,name);
				pun+=2;
				punb+=strlen(name);
				continue;
			}
			if (*(pun+1)=='2') {
				*punb='\0';
				strcat(buff,macroname);
				pun+=2;
				punb+=strlen(macroname);
				continue;
			}
			*punb='%';
		}
		else *punb=*pun;
		punb++;
		pun++;
	}
	*punb='\0';
	pun=&buff[1];
	switch (buff[0]) {
/* me */	case 'm' : write_user(user,pun); break;
/* room */	case 'r' : write_room_except(user->room,pun,user); break;
/* victim */	case 'v' : write_user(u,pun); break;
/* other */	case 'o' : write_room_except2(user->room,pun,user,u); break;
/* all   */     case 'a' : write_room(user->room,pun); break;
/* shout */     case 's' : write_room(NULL,pun); break;
		default  : write_user(user,pun); 
	}
	fgets(text,sizeof(text)-1,fp);
	user->filepos+=strlen(text);
}

if (!(strcmp(text,"#pause\n"))) {
	if (global) user->misc_op=11;
	else user->misc_op=10;
	}

if (!(strcmp(text,"#end\n"))) {
	end_macro(user);
	}
	
fclose(fp);
}

end_macro(user)
UR_OBJECT user;
{
user->misc_op=0;
user->filepos=0;
user->macro[0]='\0';
}


search_for_macro(user,str,global,syntax)
UR_OBJECT user;
char *str;
int global;
int *syntax;
{
FILE *fp;
char filename[80],name[80];
int level,pos=0;

if (global) sprintf(filename,"%s/%s",MACRODIR,GLOBALMACRO);
else sprintf(filename,"%s/%s.macro",MACRODIR,user->name);

if (!(fp=fopen(filename,"r"))) return -1;

text[0]='\0';

fgets(text,sizeof(text)-1,fp);

while (!feof(fp)) {
	if(text[0]=='@') {
		sscanf(text,"%s %d %d",name,&level,syntax);/*nome,livello,sintassi*/
		if (!(strcmp((name+1),str)) && user->level>=level) return pos;
                                                         /* trovata!! */
		}
	pos+=strlen(text);
	fgets(text,sizeof(text)-1,fp);
	}

/* niente da fare... non c'e' */
return -1;
}

macro(user)
UR_OBJECT user;
{

if (word_count<2) {
	write_user(user,"usage: macro [global/personal]\n");
	return;
	}


if (!(strncmp(word[1],"global",strlen(word[1])))) {
	list_global_macros(user);
	return;
	}

if (!(strncmp(word[1],"personal",strlen(word[1])))) {
	list_personal_macros(user);
	return;
	}


write_user(user,"Invalid option\n");
}


list_global_macros(user)
UR_OBJECT user;
{
FILE *fp,*ftemp;
char filename[80],name[80],temp[80];
int lev,level,cnt;
	
sprintf(text,"\n~BB*** Global macros available for level: %s ***~RS\n\n",level_name[user->path][user->level]);
write_user(user,text);
sprintf(filename,"%s/%s",MACRODIR,GLOBALMACRO);
if (!(fp=fopen(filename,"r"))) { 
	write_user(user,"\nNo macros available.\n\n"); 
	return;
	}

if (!(ftemp=fopen("tempfile","w"))) {
	write_user(user,"ERROR: Couldn't write tempfile.\n");
	write_syslog("ERROR: Couldn't write tempfile in gmacro().\n",0,TOSYS);
	fclose(fp);
	return;
	}

fgets(text,sizeof(text)-1,fp);

while (!feof(fp)) {
	if(text[0]=='@') fputs((text+1),ftemp);
	fgets(text,sizeof(text)-1,fp);
	}

fclose(fp);
fclose(ftemp);

if (!(ftemp=fopen("tempfile","r"))) {
	write_user(user,"ERROR: Couldn't open tempfile.\n");
	write_syslog("ERROR: Couldn't open tempfile in gmacro().\n",0,TOSYS);
	return;
	}

for(lev=NEW;lev<=user->level;++lev) {
	fseek(ftemp,0,0);
	cnt=0;
	sprintf(text,"~FT(%s)\n",level_name[user->path][lev]);
	write_user(user,text);
	text[0]='\0';
	
	fscanf(ftemp,"%s %d %*d",name,&level);

	while(!feof(ftemp)) {
		if (level==lev) { 
			sprintf(temp,"%-10s ",name);
			strcat(text,temp);
			cnt++;
			}
		if (cnt==6) {  
			strcat(text,"\n");  
			write_user(user,text);  
			text[0]='\0';  cnt=0;
			}
		fscanf(ftemp,"%s %d %*d",name,&level);
		}
	if (cnt) {
		strcat(text,"\n");  
		write_user(user,text);
		}
	}
fclose(ftemp);
unlink("tempfile");

}


list_personal_macros(user)
UR_OBJECT user;
{
FILE *fp,*ftemp;
char filename[80],name[80],temp[80];
int cnt;
	
write_user(user,"\n~BB*** Your personal macros ***~RS\n\n");

sprintf(filename,"%s/%s.macro",MACRODIR,user->name);
if (!(fp=fopen(filename,"r"))) { 
	write_user(user,"\nNo macros.\n\n"); 
	return;
	}

if (!(ftemp=fopen("tempfile","w"))) {
	write_user(user,"ERROR: Couldn't write tempfile.\n");
	write_syslog("ERROR: Couldn't write tempfile in pmacro().\n",0,TOSYS);
	fclose(fp);
	return;
	}

fgets(text,sizeof(text)-1,fp);

while (!feof(fp)) {
	if(text[0]=='@') fputs((text+1),ftemp);
	fgets(text,sizeof(text)-1,fp);
	}

fclose(fp);
fclose(ftemp);

if (!(ftemp=fopen("tempfile","r"))) {
	write_user(user,"ERROR: Couldn't open tempfile.\n");
	write_syslog("ERROR: Couldn't open tempfile in pmacro().\n",0,TOSYS);
	return;
	}

cnt=0;
text[0]='\0';
	
fscanf(ftemp,"%s %*d %*d",name);

while(!feof(ftemp)) {
	sprintf(temp,"%-10s ",name);
	strcat(text,temp);
	cnt++;
	if (cnt==6) {  
		strcat(text,"\n");  
		write_user(user,text);  
		text[0]='\0';  cnt=0;
		}
	fscanf(ftemp,"%s %*d %*d",name);
	}

if (cnt) {
	strcat(text,"\n");  
	write_user(user,text);
	}

fclose(ftemp);
unlink("tempfile");

}


send_banner(user)
UR_OBJECT user;
{
FILE *fp;
char filename[80];
char line[122];
UR_OBJECT u;
int flag=0;

if (com_num==SEE) {
	if (word_count<2) strcpy(word[1],"bannerlist");
	sprintf(filename,"%s/%s.banner",BANNERDIR,word[1]);
	if (!(fp=fopen(filename,"r"))) {
		write_user(user,"This banner does not exist...\n");
		return;
		}
	fgets(line,120,fp);
	while (!feof(fp)) {
		write_user(user,line);
		fgets(line,120,fp);
		}
	fclose(fp);
	return;
	}

if (user->muzzled) {
	write_user(user,"You are muzzled, you can't send banners...\n");
	return;
	}

if (word_count<3) {
        write_user(user,"Usage: .send <user> <banner name>\n");
	return;
        }

if (!(strcmp(word[1],"all"))) flag=1;
if (!(strcmp(word[1],"allin"))) flag=2;

if ((u=get_user(word[1]))==NULL && !flag) {
	write_user(user,notloggedon);
	return;
	}

if (!flag) {
	if (u==user) {
		write_user(user,"Why don't you try .see <banner> instead?\n");
		return;
		}
 
	if (u->ignall) {
		write_user(user,"He is ignoring everyone at the moment...\n");
		return;
		}

	if (u->afk) {
		write_user(user,"He is afk at the moment...\n");
		return;
		}

	if (u->room==NULL) {
	        sprintf(text,"%s is offsite.\n",u->name);        
		write_user(user,text);
	        return;
	        }
	}


sprintf(filename,"%s/%s.banner",BANNERDIR,word[2]);

if (!(fp=fopen(filename,"r"))) {
	write_user(user,"This banner does not exist...\n");
	return;
	}

if (!flag) {
	fgets(line,120,fp);
	while (!feof(fp)) {
		write_user(u,line);
		fgets(line,120,fp);
		}
	sprintf(text,"~FM************* From %s to You *************\n",user->name);
	write_user(u,text);
	}
else {
	for (u=user_first;u!=NULL;u=u->next) {
		if (u==user || u->ignall || u->afk || u->room==NULL ||
                   (flag==2 && u->room!=user->room)) continue;
		fgets(line,120,fp);
		while (!feof(fp)) {
			write_user(u,line);
			fgets(line,120,fp);
			}
		sprintf(text,"~FM************* From %s to All *************\n",user->name);
		write_user(u,text);
		fseek(fp,0,0);
		}
	}

fclose(fp);	
write_user(user,"Banner sent...\n");
}























/**************************** EVENT FUNCTIONS ******************************/

void do_events()
{
set_date_time();
check_reboot_shutdown();
check_idle_and_timeout();
check_nethangs_send_keepalives(); 
check_messages(NULL,0);
check_macros();
reset_alarm();
}


check_macros() {
UR_OBJECT user;
        user=user_first;
        while(user!=NULL) {
                if (user->misc_op==10) run_macro(user,0);
		if (user->misc_op==11) run_macro(user,1);
                user=user->next;
		}
}


reset_alarm()
{
signal(SIGALRM,do_events);
alarm(heartbeat);
}



/*** See if timed reboot or shutdown is underway ***/
check_reboot_shutdown()
{
int secs;
char *w[]={ "~FRShutdown","~FYRebooting" };

if (rs_user==NULL) return;
rs_countdown-=heartbeat;
if (rs_countdown<=0) talker_shutdown(rs_user,NULL,rs_which);

/* Print countdown message every minute unless we have less than 1 minute
   to go when we print every 10 secs */
secs=(int)(time(0)-rs_announce);
if (rs_countdown>=60 && secs>=60) {
	sprintf(text,"~OLSYSTEM: %s in %d minutes, %d seconds.\n",w[rs_which],rs_countdown/60,rs_countdown%60);
	write_room(NULL,text);
	rs_announce=time(0);
	}
if (rs_countdown<60 && secs>=10) {
	sprintf(text,"~OLSYSTEM: %s in %d seconds.\n",w[rs_which],rs_countdown);
	write_room(NULL,text);
	rs_announce=time(0);
	}
}



/*** login_time_out is the length of time someone can idle at login, 
     user_idle_time is the length of time they can idle once logged in. 
     Also ups users total login time. ***/
check_idle_and_timeout()
{
UR_OBJECT user,next;
int tm;

/* Use while loop here instead of for loop for when user structure gets
   destructed, we may lose ->next link and crash the program */
user=user_first;
while(user) {
	next=user->next;
	if (user->type==CLONE_TYPE) {  user=next;  continue;  }
	user->total_login+=heartbeat; 
	if (user->level>time_out_maxlevel) {  user=next;  continue;  }

	tm=(int)(time(0) - user->last_input);
	if (user->login && tm>=login_idle_time) {
		write_user(user,"\n\n*** Time out ***\n\n");
		disconnect_user(user);
		user=next;
		continue;
		}
	if (user->warned) {
		if (tm<user_idle_time-60) {  user->warned=0;  continue;  }
		if (tm>=user_idle_time) {
			write_user(user,"\n\n\07~FR~OL~LI*** You have been timed out. ***\n\n");
			disconnect_user(user);
			user=next;
			continue;
			}
		}
	if ((!user->afk || (user->afk && time_out_afks)) 
	    && !user->login 
	    && !user->warned
	    && tm>=user_idle_time-60) {
		write_user(user,"\n\07~FY~OL~LI*** WARNING - Input within 1 minute or you will be disconnected. ***\n\n");
		user->warned=1;
		}
	user=next;
	}
}
	


/*** See if any net connections are dragging their feet. If they have been idle
     longer than net_idle_time the drop them. Also send keepalive signals down
     links, this saves having another function and loop to do it. ***/
check_nethangs_send_keepalives()
{
NL_OBJECT nl;
int secs;

for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type==UNCONNECTED) {
		nl->warned=0;  continue;
		}

	/* Send keepalives */
	nl->keepalive_cnt+=heartbeat;
	if (nl->keepalive_cnt>=keepalive_interval) {
		write_sock(nl->socket,"KA\n");
		nl->keepalive_cnt=0;
		}

	/* Check time outs */
	secs=(int)(time(0) - nl->last_recvd);
	if (nl->warned) {
		if (secs<net_idle_time-60) nl->warned=0;
		else {
			if (secs<net_idle_time) continue;
			sprintf(text,"~OLSYSTEM:~RS Disconnecting hung netlink to %s in the %s.\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			shutdown_netlink(nl);
			nl->warned=0;
			}
		continue;
		}
	if (secs>net_idle_time-60) {
		sprintf(text,"~OLSYSTEM:~RS Netlink to %s in the %s has been hung for %d seconds.\n",nl->service,nl->connect_room->name,secs);
		write_level(ARCH,1,text,NULL);
		nl->warned=1;
		}
	}
destructed=0;
}



/*** Remove any expired messages from boards unless force = 2 in which case
	just do a recount. ***/
check_messages(user,force)
UR_OBJECT user;
int force;
{
RM_OBJECT rm;
FILE *infp,*outfp;
char id[82],filename[80],line[82];
int valid,pt,write_rest;
int board_cnt,old_cnt,bad_cnt,tmp;
static int done=0;

switch(force) {
	case 0:
	if (mesg_check_hour==thour && mesg_check_min==tmin) {
		if (done) return;
		}
	else {  done=0;  return;  }
	break;

	case 1:
	printf("Checking boards...\n");
	}
done=1;
board_cnt=0;
old_cnt=0;
bad_cnt=0;

for(rm=room_first;rm!=NULL;rm=rm->next) {
	tmp=rm->mesg_cnt;  
	rm->mesg_cnt=0;
	sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
	if (!(infp=fopen(filename,"r"))) continue;
	if (force<2) {
		if (!(outfp=fopen("tempfile","w"))) {
			if (force) fprintf(stderr,"NUTS: Couldn't open tempfile.\n");
			write_syslog("ERROR: Couldn't open tempfile in check_messages().\n",0,TOSYS);
			fclose(infp);
			return;
			}
		}
	board_cnt++;
	/* We assume that once 1 in date message is encountered all the others
	   will be in date too , hence write_rest once set to 1 is never set to
	   0 again */
	valid=1; write_rest=0;
	fgets(line,82,infp); /* max of 80+newline+terminator = 82 */
	while(!feof(infp)) {
		if (*line=='\n') valid=1;
		sscanf(line,"%s %d",id,&pt);
		if (!write_rest) {
			if (valid && !strcmp(id,"PT:")) {
				if (force==2) rm->mesg_cnt++;
				else {
					/* 86400 = num. of secs in a day */
					if ((int)time(0) - pt < mesg_life*86400) {
						fputs(line,outfp);
						rm->mesg_cnt++;
						write_rest=1;
						}
					else old_cnt++;
					}
				valid=0;
				}
			}
		else {
			fputs(line,outfp);
			if (valid && !strcmp(id,"PT:")) {
				rm->mesg_cnt++;  valid=0;
				}
			}
		fgets(line,82,infp);
		}
	fclose(infp);
	if (force<2) {
		fclose(outfp);
		unlink(filename);
		if (!write_rest) unlink("tempfile");
		else rename("tempfile",filename);
		}
	if (rm->mesg_cnt!=tmp) bad_cnt++;
	}
switch(force) {
	case 0:
	if (bad_cnt) 
		sprintf(text,"CHECK_MESSAGES: %d files checked, %d had an incorrect message count, %d messages deleted.\n",board_cnt,bad_cnt,old_cnt);
	else sprintf(text,"CHECK_MESSAGES: %d files checked, %d messages deleted.\n",board_cnt,old_cnt);
	write_syslog(text,1,TOSYS);
	break;

	case 1:
	printf("  %d board files checked, %d out of date messages found.\n",board_cnt,old_cnt);
	break;

	case 2:
	sprintf(text,"%d board files checked, %d had an incorrect message count.\n",board_cnt,bad_cnt);
	write_user(user,text);
	sprintf(text,"%s forced a recount of the message boards.\n",user->name);
	write_syslog(text,1,TOSYS);
	}
}
/**************************** Made in England *******************************/
