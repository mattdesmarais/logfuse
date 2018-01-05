/*
  m.desmarais@f5.com 
  - Merg multiple logs into a single sorted file 
  - Merg a single file containings mulitple logs into a single sorted file 

Known Issues:
BZ1: 
Huge problem with the merge() function. For some reason when there are many enteries 
there's a segmentation fault. I commented out the function call to merg() and it no longer crashes
(but then it no longer sorts ;) 


BZ2: 
Memory footprint is really large ?? 


To do: 
Makefile 
Changelist File
Commint via git
Create a page on docs site 
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h> 
#include <locale.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>
#include <libgen.h> 
#include <regex.h> 
#include "magic.h"

#define __USE_XOPEN
#include <time.h> 
#include <sys/time.h>
#include <glob.h>


#define LINESIZE 1024
#define MAXFILES 524288000

//Linked List structure
struct node
{
	char* basename; 
	char* datestring;
	char* timestring;
	int ms; 
	char* message;
	time_t epochtime; 
	struct node *next, *prev;	
};
typedef struct node node;

node *head = NULL;  //declar pointer beginning of linked list

//prototypes for linked list functions   
struct node* createnewnode(char* ds, char* ts, char* msg, char* basename); //create a new node and return a pointer to it
void insert_at_head(char* ds, char* ts, char* msg, char* basename);
static void clean(); 
int parseFile(char* fname);

//utility functions 
int lensum(char **input);
char* concat(const char *s1, const char *s2);
void print_elements();
void debug_elements();
int print_elementFile(char* fname);
char* tidyline(char* pline);

int detectDelimiter(char* s);
void rem_markup(char *s, int mode);
int verifydatetime(char *d, char *t);
//char* rem_markup(char *s);

int globerr(const char *path, int eerrno);

//prototypes for merge sort 
struct node* merge(struct node *first, struct node *second);
struct node* mergeSort(struct node *head);
struct node* split(struct node *head);

//Beautify functions
void usage();
char* readable_fs(double size/*in bytes*/, char *buf);

/* DOUBLY LINKED LISTS */

//Node Generator 
struct node* createnewnode(char* ds, char* ts, char* msg, char* basename)
{
	node *newnode = malloc(sizeof(node));  //allocate memory on the heap and create a new node structure 

	//time_t tt = malloc(sizeof(time_t)); 
	time_t epochtime; 

	
	char *tds = malloc(10); 
	char *tts = malloc(10);
	char *tmsg = malloc(LINESIZE); 
	char *bname = malloc(LINESIZE);
	

	int mss; 
	int slen; 
	struct tm tm; 
	char *stmp = ds; 
	
	strcpy(tds, ds); 
	strcpy(tts, ts); 
	strcpy(tmsg, msg); 
	strcpy(bname, basename);

	//concatinate date and time for time tm
	strcat(stmp, " ");
	strcat(stmp, ts);	
	
	if (strptime(stmp, "%Y-%m-%d %H:%M:%S", &tm) == NULL)
	{
		fprintf(stderr, "Date ErrorRrrrrr\n");
	}

	//Convert to time epoch 
	tm.tm_isdst = -1;
	epochtime = mktime(&tm);
	if (epochtime == -1) {
		fprintf(stderr, "Date ErrorRrrrrr\n");
	}

	/*
	The client side logs produced by our VPN clients have a millisecond value. I'm using to use
	this in the sorting algorithm to properly order them. 
	*/

	slen = strlen(ts);
	if (slen == 12)
	{
		//timestamp has a millisecond value
		mss = atoi(&ts[strlen(ts) - 3]);	
	} else {
		mss = 000;
		//append 000 to the timestring, so it aligns in the logs
		strcat(tts, ":000");

	}

	//Set values in structure 
	newnode->basename = bname;
	newnode->epochtime = epochtime;
	newnode->datestring = tds;
	newnode->timestring = tts;
	newnode->message = tmsg;
	newnode->ms = mss;


	newnode->prev = NULL;	 
	newnode->next = NULL;   

	return newnode;
}


//Utility function to insert a new node at the beginning of the
//doubly linked list 
void insert_at_head(char* ds, char* ts, char* msg, char* basename)
{ 
#ifdef TRACE
	printf("%s %s %s\n",ds,ts,msg);
#endif

	node *newnode = createnewnode(ds, ts, msg, basename);

	//If this is the first element being added to the linked list 
	if (head == NULL)
	{
		head = newnode;
		return;
	}
	head->prev = newnode;
	newnode->next = head;
	head = newnode; 
}

/* 
Utility function to print a doubly linked list - stdout 
*/

void print_elements() 
{

	node *ptmp = head; //beginning of the list 
	if (ptmp == NULL)
	{
		return;
	}

	while(ptmp != NULL)
	{
		printf("%s %s [%s] %s\n",ptmp->datestring,ptmp->timestring,ptmp->basename, ptmp->message );
		ptmp = ptmp->next; 
	}
	return; 
}


void debug_elements() 
{

	node *ptmp = head; //beginning of the list 
	if (ptmp == NULL)
	{
		return;
	}

	while(ptmp != NULL)
	{
		printf("debug_elements: %p\n", ptmp );
		ptmp = ptmp->next; 
	}
	return; 
}

/* 
Utility function to print a doubly linked list - to filename 
*/
int print_elementFile(char* fname) 
{
	FILE *ofp;
	node *ptmp = head; 

	if (ptmp == NULL)
	{
		return -1;
	}

	ofp = fopen(fname, "w+"); 

	if (ofp == NULL)
	{
		return -1; 
	} else {
		while(ptmp != NULL)
		{
			fprintf(ofp, "%s %s [%s] %s\n",ptmp->datestring, ptmp->timestring, ptmp->basename, ptmp->message);
			ptmp = ptmp->next;
		}
	}
	fclose(ofp);
	return 0;
}


/*
Utility function to delete all enteries from the linked list. This is very
important or we'd have a huge memory leak
*/


static void clean() 
{
	node *temp = NULL; 

	while(head != NULL) 
	{
		temp = head; 
		head = head->next; 
		free(temp->datestring);
		free(temp->timestring);
		free(temp->message);
		free(temp->basename);

		//do i need to free this here ??? i'm confused
		//free(temp->epochtime);
		//free(temp->ms); 
		free(temp);
	}
}


/*
Operation #1 - Utility function to remove markup and stuff (HTML, Javascript, Exported logs) between the markup tags 
			 - Remove Special tags. For examples in CLF (Common Log Format), we have [] ex, [10/Oct/2000:13:55:36 -0700]

Operation #2 - Detect if there's white space at the beginning of the char array and remove it
*/

void rem_markup(char *strin, int mode)
//char* rem_markup(char *strin)
{
int i=0, j=0, z=0;
int ws = 0, ns = 0; 


char cupdate[LINESIZE];	// buffer that holds cleaned text
char nospace[LINESIZE]; // buffer that holds cleaned text with prefixed spaces deleted

	
if (mode == 1)
{
	//Remove CLF characters 
	while(strin[i] != '\0') {
		if ((strin[i]=='[') || (strin[i]==']') )
		{
			i++; //skip 
		} else {
			cupdate[j] = strin[i];
			j++;
			i++;
		}
	 
	}

} else {
	while(strin[i] != '\0') {
		
		if (strin[i] == '<')
		{

			//must not add this to new char array
			//must skip all characters until we see a close tag > 
			while(strin[i] != '>') {
				i++;
			}

		} else {
			cupdate[j] = strin[i];

				j++;
		}
		i++;
	}
	cupdate[++j] = '\0'; 
}


/*
	Debug logs such as tmm and rewrite have the following structure 
	==> tmm.8 <==
	<13> Jul 27 08:04:47 bigip12 notice CMI: Using certificate: /config/filestore/files_d/Common_d/trust_certificate_d/:Common:dtdi.crt_36431_2
	<13> Jul 27 08:04:47 bigip12 notice CMI: Using key: /config/filestore/files_d/Common_d/trust_certificate_key_d/:Common:dtdi.key_36433_2
*/

	while(isspace(cupdate[z]) != 0) {
		ws++;
		z++;
	}


	if (ws == 0)
	{	
		//strcpy(strLinPtr,cupdate);
		strcpy(strin,cupdate);
		memset(cupdate, 0, sizeof cupdate);			
	} else {

		//build another char array with no spaces 
		for (int i = ws; cupdate[i] != '\0'; i++)
		{
			nospace[ns] = cupdate[i];
			ns++;
		}
		//strcpy(strLinPtr,nospace);
		strcpy(strin,nospace);

		memset(nospace, 0, sizeof nospace);	
	}	

	//return strLinPtr;

}

/*
Tries to detect if the delimiter is a comma (CSV) or space 
*/
int detectDelimiter(char* strin)
{
	int ret; 
	int i = 0;

	while((strin[i] != '\0') || (i <= 0)) {


		if (strin[i] == 'T')
				{
					/* 
					T - bootmarker delimiter. Also need to detect the timezone delimiter + or -, ex: 
					2017-11-16T07:06:25-08:00 or 2017-10-08T23:52:47+00:00
					*/

					while(strin[i] != '\0') {
						if (strin[i] == '+')
						{
							ret = 3; 
							break;
						}

						if (strin[i] == '-')
						{
							ret = 4; 
							break;
						}

					i++;

					}
				  break;
				} 	

		//printf("%c",strin[i]);
		if (strin[i] == ',')
		{
			//comma detected
			//printf("COMMA:%c",strin[i]);
			ret = 1; 
			break; 
		} 

		if (strin[i] == ' ')
		{
			//space detected
			ret = 2; 
			break; 
		} 
		
	  i++;
	}

	return ret;
}

/*
Verifies the length of the date and time
*/

int verifydatetime(char *d, char *t)
{
/*
verify date which should be - 10 characters 2017-11-13 
*/
	if (strlen(d) == 10 || strlen(d) == 9)
	{
		//date format is good. Verify time
		switch(strlen(t))
		{
		  case 12:
		  	//10:57:19:000
		  	return 1; 
		  case 8:
		  	//10:57:19
		    return 1; 
		  case 9:
		  	/* 
		  		Possibility for times that are not formated well - 07:09:49:

		  		1. Check if the last char is semi-colon. If so strip it. 
		  		2. Else return an error 
		    */
		    t[strlen(t)-1] = 0;
		    return 1; 

		  default :
		  	return 0; 
		}

	} else {
		return 0; 
	}
  return 0;

}


/*
Parse File 
*/
int parseFile(char* fname)
{

    FILE *in = fopen(fname, "r");  //add some error checking here. What if its null ?? Main driver checks that file exists already.
    char line[LINESIZE];
    char bline[LINESIZE]; //copy of the current line 

    struct tm date;
 	char *tok; 
 	char dbuf[10], tbuf[11], mbuf[LINESIZE], monthbuf[10], daybuf[10], tz[10], yearbuf[10];
 	//char dbuf[LINESIZE], tbuf[LINESIZE], mbuf[LINESIZE], monthbuf[LINESIZE], daybuf[LINESIZE], tz[LINESIZE], yearbuf[LINESIZE];
 	//Common Logging Format specific buffers
 	char clfhbuf[10],clfidbuf[10],clfusrbuf[10],clfdatetime[10],clfmsg[LINESIZE];

	const char * months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
 	int monthnum;
 	int monthflg = 0; 

 	int pos;

 	int bufSize = 11; 
 	char *buf = malloc(bufSize);
 
 	//char *strLinPtr; 

 	/*
	Construct filename to use in the sorted logs, ex: 
	2017-11-13  8:43:26:000 LOG~TMM1.2 48793,48793,, 48, , 151, GetMacAddresses(), testing ifname=p2p0
	2017-11-13  8:43:27:000 LOG~LTM.10 48793,48793,, 48, , 151, GetMacAddresses(), testing ifname=p2p0
 	*/
 
 	char *base;
 	char basebuff[11];
 	base = basename(fname);

 	snprintf(basebuff, sizeof(basebuff), "%c%c%c%c%c%c%c%c",toupper(base[0]),toupper(base[1]),toupper(base[2]),toupper(base[3]),toupper(base[4]),toupper(base[5]),toupper(base[6]),toupper(base[7]));

 	/* 
 	Needed for BIGIP logs, as most don't have a year. This will look 
 	https://linux.die.net/man/2/stat
	time_t    st_mtime;   
	*/
 	struct stat attrib;
 	stat(fname, &attrib);
 	char ftime[5];
 	int delimiterNum; 
 	

 	strftime(ftime, 5, "%Y", localtime(&attrib.st_mtime));

 	
    while (fgets(line, sizeof(line), in) != NULL) {

#ifdef TRACE 	 
  	 printf("Trace Line: %s\n",line);
#endif

  	 //strLinPtr = rem_markup(line); 
 	 rem_markup(line, 0);
 
     if ((void *)strptime(line,"%Y-%m-%d",&date) != NULL)
       {    


	       	  delimiterNum = detectDelimiter(line);


	       	  if (delimiterNum == 1)
	       	  {
			       	  /*
			       	  	Support comma seperated log messages. Used for F5 client side logs (EdgeClient/F5Access).
			       	  */
			      	  
			       	  tok = strtok(line, ","); // initialize the string tokenizer and receive pointer to first token


				       strcpy((char*)dbuf, tok);
				      
				       tok = strtok(NULL, ",");

					if (NULL != tok)
					{
						strcpy((char*)tbuf,tok);
					}

				     tok = strtok(NULL,"\n");  //everything else must be the message, tokenize to the newline character \n 

				    
				       if (NULL != tok)
				       {
				       		strcpy((char*)mbuf, tok);
				       }
	      
	       	  } else if (delimiterNum == 2) {
			       	  /*
			       	  	Support space seperated log messages.
			       	  */
			      	  
			       	  tok = strtok(line, " "); 


				       strcpy((char*)dbuf, tok);
				      
				       tok = strtok(NULL, " ");

					if (NULL != tok)
					{
						strcpy((char*)tbuf,tok);
					}

				     tok = strtok(NULL,"\n");  

				    
				       if (NULL != tok)
				       {
				       		strcpy((char*)mbuf, tok);
				       }
	       	  } else if (delimiterNum == 3 || delimiterNum == 4 ) {

					//support bootmarker format: 2017-11-16T07:06:25-08:00 or 2017-10-08T23:52:47+00:00 
  	 				tok = strtok(line, "T"); 
  	 				strcpy((char*)dbuf, tok);

  	 				if (delimiterNum == 3)
  	 				{
  	 					tok = strtok(NULL, "+"); 
  	 				} else {
  	 					tok = strtok(NULL, "-"); 
  	 				}

  	 				if (NULL != tok)
					{
						strcpy((char*)tbuf,tok);
					}
					tok = strtok(NULL, " ");

			 		if (NULL != tok)
					{
						strcpy((char*)tz,tok);
					}			

					tok = strtok(NULL,"\n"); 

				    if (NULL != tok)
				    {
				        strcpy((char*)mbuf, tok);
				    }


	       	  } else {
	       	  	  //error 
	       	  	  continue;
	       	  }

	 
	       	//Verification that date and time are in the correct format before add to linked list
			if ( (strptime(dbuf,"%Y-%m-%d",&date) != NULL) && strptime(tbuf,"%H:%M:%S",&date) != NULL) {


					int vret = verifydatetime(dbuf,tbuf);
				
					if (vret != 0)
					{

						insert_at_head(dbuf, tbuf, mbuf, basebuff);

					} else {
						continue;
					}

			} else {

				continue;
			}


       } else if ((void *)strptime(line,"%b %d",&date) != NULL) {

     	  /*
			Most troubleshooting logs (ltm,tmm,apm,asm,audit..etc) use a format like "Nov 14 03:07:14" (https://tools.ietf.org/html/rfc2822#page-14)
			, which does not contain a date. The best approach that i can think of is to use the last-modified time on the file to try and perdict the year. 

			 	strftime(ftime, 10, "%Y", localtime(&attrib.st_mtime));
			
			Then insert that into the unified date format that i'm using (YYYY-MM-DD)

       	  */

       		monthflg = 0;

    		tok = strtok(line, " "); // initialize the string tokenizer and receive pointer to first token

	       strcpy((char*)monthbuf, tok);

	       tok = strtok(NULL, " ");

			if (NULL != tok)
			{
				strcpy((char*)daybuf,tok);
			}


		   	tok = strtok(NULL, " ");
		   	if (NULL != tok)
			{
				strcpy((char*)tbuf,tok);
			}


			tok = strtok(NULL,"\n");  

	    
	       if (NULL != tok)
	       {
	       		strcpy((char*)mbuf, tok);

	       }

			//Convert month name to number 
			for (int i = 0; i <= 12; i++)
			{
				if (strcmp(monthbuf,months[i])==0)
				{
					monthnum = i+1;
					monthflg = 1;  //set month flag 
					break;
				}
	
			}

			if (monthflg != 0)
			{			
			  if (monthnum < 10 )
			  {
			  	//concatination mutliple strings into YYYY-MM-DD format 
				if(snprintf(buf, bufSize, "%s-0%d-%s", ftime,monthnum, daybuf) >= bufSize){
				        bufSize *= 2;
				        free(buf);
				        buf = malloc(bufSize);

				        if(snprintf(buf, bufSize, "%s-0%d-%s", ftime,monthnum, daybuf) >= bufSize){
				            printf("Not enough space. Aborting\n");
				            exit(1);
				        }
				    }
               } else {
				   if(snprintf(buf, bufSize, "%s-%d-%s", ftime,monthnum, daybuf) >= bufSize){
				        bufSize *= 2;
				        free(buf);
				        buf = malloc(bufSize);

				        if(snprintf(buf, bufSize, "%s-%d-%s", ftime,monthnum, daybuf) >= bufSize){
				            printf("Not enough space. Aborting\n");
				            exit(1);
				        }
				    }
               }



            /* 
            	There's also one case that comes up with debug logs, where the day is 8 instead of 08. This causes
            	a problem in the output as it's not well formated. These time stamps should be unified. 
            */

	       	//Verification that date and time are in the correct format before add to linked list.

				if ( (strptime(buf,"%Y-%m-%d",&date) != NULL) && strptime(tbuf,"%H:%M:%S",&date) != NULL) {

						/*
						   Still seeing conditions that strptime is not catching. Maybe a bug in this function. 
						   I'm going to do a hard length strlen calculation. I could be pedantic and use sscanf,
						   but lets stick with this technique for right now. 

						*/

						int vret = verifydatetime(buf,tbuf);

						if (vret != 0)
						{
							insert_at_head(buf, tbuf, mbuf, basebuff);

						} else {

							continue;
						}

				} else {
				
					continue; 
				}
			
			} 

		}  else 
		/*
			Support more log formats here 
			else if ((void *)strptime(line,"format specifiers",&date) != NULL) 
		*/

		{ 

			/*
				If the line does not start with a date or time, then it cannot be detected by strptime. I notied there are many log 
				formats like this, however the most popular is CLF (Common logging format), used with apache (https://httpd.apache.org/docs/trunk/logs.html#common).
				By default the formats are: 

				127.0.0.1 - frank [10/Oct/2000:13:55:36 -0700] "GET /apache_pb.gif HTTP/1.0" 200 2326
				[Fri Sep 09 10:42:29.902022 2011] [core:error] [pid 35708:tid 4328636416] [client 72.15.99.187] File does not exist: /usr/local/apache2/htdocs/favicon.ico

				However it seems that this can be customized. For example : 
				LogFormat "%h %l %u %t \"%r\" %>s %b"
				LogFormat "%u %t %h %l \"%r\" %>s %b"
 
			*/

			//Apach Common Log format 
			int clf = 0;
			if (line != NULL && strlen(line) !=0)
			{
				rem_markup(line, 1);  //clean line 
				strncpy(bline, line, strlen(line)); //create a copy 

				//Attempt to find CLF and other log formats 
				pos = 0; 
				tok = strtok(line, " ");
				while(tok != NULL)
				{
					if((void *)strptime(tok,"%d/%b/%Y:%H:%M:%S",&date) != NULL) {
						//[10/Oct/2000:13:55:36 -0700]
						clf = 1;
						break;
					} 
					pos++;
					tok = strtok(NULL, " ");
				}

				if (clf !=0)
				{
					tok =  strtok(bline," ");

					if (NULL != tok)
					{
						strcpy((char*)clfhbuf,tok); //host 
					}

					tok = strtok(NULL, " ");
				   	if (NULL != tok)
					{
						strcpy((char*)clfidbuf,tok); //id 
					}

					tok = strtok(NULL, " ");
				   	if (NULL != tok)
					{
						strcpy((char*)clfusrbuf,tok); //user 
					}

					tok = strtok(NULL, " ");
				   	if (NULL != tok)
					{
						strcpy((char*)clfdatetime,tok); //date & time 
					}

					tok = strtok(NULL, " ");
				   	if (NULL != tok)
					{
						strcpy((char*)tz,tok); //timezone (will be dropped)
					}
		
					tok = strtok(NULL, "\n");
				   	if (NULL != tok)
					{
						strcpy((char*)clfmsg,tok); //message  
					}

					/* 
					Date/Time is now in format 13/Nov/2017:06:01:07 , %Y-%m-%d %H:%M:%S
					Need to tokenize clfdatetime
					*/

					tok = strtok(clfdatetime,"/");
					if (NULL != tok)
					{
						strcpy((char*)daybuf,tok);
					}
					tok = strtok(NULL,"/");
					if (NULL != tok)
					{
						strcpy((char*)monthbuf,tok);
						//Convert month name to number 
						for (int i = 0; i <= 12; i++)
						{
							if (strcmp(monthbuf,months[i])==0)
							{
								monthnum = i+1;
								monthflg = 1;  //set month flag 
								break;
							}
				
						}
					}					
					tok = strtok(NULL,":");
					if (NULL != tok)
					{
						strcpy((char*)yearbuf,tok);
					}
					tok = strtok(NULL,"\n");
					if (NULL != tok)
					{
						strcpy((char*)tbuf,tok);
					}

					//concatinate date - buf 
					if(snprintf(buf, bufSize, "%s-%d-%s", yearbuf,monthnum, daybuf) >= bufSize){
				        bufSize *= 2;
				        free(buf);
				        buf = malloc(bufSize);
				    }

					if ( (strptime(buf,"%Y-%m-%d",&date) != NULL) && strptime(tbuf,"%H:%M:%S",&date) != NULL) {

							/*
							   Still seeing conditions that strptime is not catching. Maybe a bug in this function. 
							   I'm going to do a hard length strlen calculation. I could be pedantic and use sscanf,
							   but lets stick with this technique for right now. 

							*/

							int vret = verifydatetime(buf,tbuf);

							if (vret != 0)
							{
								insert_at_head(buf, tbuf, clfmsg, basebuff);

							} else {

								continue;
							}

					} else {
					
						continue; 
					}


				}


			}

		}

    }
    fclose(in); //close file pointer
    //free(strLinPtr);

 return 0;   
}

/*
  MergSort Algorithm Functions 
*/

// Split a doubly linked list into 2 DLLs of half sizes
struct node *split(struct node *head)
{
    struct node *fast = head,*slow = head;
    while (fast->next && fast->next->next)
    {
        fast = fast->next->next;
        slow = slow->next;
    }
    struct node *temp = slow->next;
    slow->next = NULL;
    return temp;
}

// Function to do merge sort
struct node *mergeSort(struct node *head)
{
    if (!head || !head->next)
        return head;
    struct node *second = split(head);

#ifdef MERGETRACE
	printf("mergeSort1 head (%p) || second (%p)\n",head, second );    
#endif

    // Recur for left and right halves
    head = mergeSort(head);
    second = mergeSort(second);

#ifdef MERGETRACE
	printf("mergeSort2 head (%p) || second (%p)\n",head, second );    
#endif
    // Merge the two sorted halves

#ifdef MERGETRACE
	if ((head == NULL) || (second == NULL) )
	{
		printf("mergeSort2 error -  head (%p) || second (%p)\n",head, second );  
	}
	  
#endif

  	 return merge(head,second);	//Check that pointers are not NULL

   
}

/* 
Function to merge two linked lists. This is where the sort critera goes
*/
struct node* merge(struct node *first, struct node *second)
{
#ifdef MERGETRACE
	printf("mergdebug1 - head(%p) || second(%p)\n",first,second );
#endif

    // If first linked list is empty
    if (!first)
        return second;
 
    // If second linked list is empty
    if (!second)
        return first;
 
    // Pick the smaller value
    if (first->epochtime < second->epochtime)
    {
        first->next = merge(first->next,second);
        first->next->prev = first;
        first->prev = NULL;
        return first;
    }
    else
    {
        second->next = merge(first,second->next);
        second->next->prev = second;
        second->prev = NULL;
        return second;
    }
}

/*
File size 
*/
char* readable_fs(double size/*in bytes*/, char *buf) {
    int i = 0;
    const char* units[] = {"B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
    while (size > 1024) {
        size /= 1024;
        i++;
    }
    sprintf(buf, "%.*f %s", i, size, units[i]);
    return buf;
}

/* globerr --- print error message for glob() */

int globerr(const char *path, int eerrno)
{
  //fprintf(stderr, "TEST::%s: %s: %s\n", myname, path, strerror(eerrno));
  return 0; /* let glob() keep going */
}


/*
Usage Instructions.
*/
void usage()
{
        fprintf(stderr, "Usage: logfuse <arguments> <filename>\n");

		fprintf(stderr, "\nArguments are optional:\n");
		fprintf(stderr, "\t-h     Show this help message\n");
		fprintf(stderr, "\t-o     Output to a file (defaults to stdout)\n");
		fprintf(stderr, "\t-l     Specify a list of files to fuse\n");

		fprintf(stderr, "\nSupported Filetypes:\n");
		fprintf(stderr, "\tPlain Text\n");
		fprintf(stderr, "\tPlain Text with Markup (html,xml)\n");
		fprintf(stderr, "\tGzip\n");
		fprintf(stderr, "\tZip Archive (zip in a zip is not supported)\n");

        fprintf(stderr, "\nSupported Date/Time Formats::\n");
        fprintf(stderr, "\tNov 23 05:27:32       	  BIGIP Style Logs (ltm,apm,tmm,asm..etc)\n");
        fprintf(stderr, "\t2017-08-30,18:07:15:173       EdgeClient & F5Access\n");
        fprintf(stderr, "\t[10/Oct/2000:13:55:36 -0700]  Apache (in development)\n");

        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "\tlogfuse f5EdgeReport.html\n");
        fprintf(stderr, "\tlogfuse -o mlog1.txt F5AccessReport.zip\n");
        fprintf(stderr, "\tlogfuse -o mlog2.txt -l apm.1.gz tmm ltm edge.out F5Access.log\n");
        fprintf(stderr, "\tlogfuse -o mlog3.txt testfiles/tm*\n");
        fprintf(stderr, "\tlogfuse -o mlog4.txt F?Access-*.log\n");
        fprintf(stderr, "\t(Oi! Wildcard Globs (*,?) cannot be used with the lists (-l) argument)\n\n\n");
    
}

/*
Main Driver 
*/
int main(int argc, char *argv[])
{
	char *ofile; 
	int ofilef = 0; //0 = stdout , 1 = file stream 
	int pret; 
	magic_t myt = magic_open(MAGIC_ERROR|MAGIC_MIME_TYPE); //check file against magic database templates 
	char *base; //used to store base filename

	srand(time(NULL));
	int ftoken = rand();


	//Globber Support Code Start 
	int i;
	glob_t results;
	int gret;
	//Globber Support Code End 





	//process input arguments 
	if (argc <= 1)
	{
		goto ERRORU;
	} else {

		if (strcmp(argv[1],"-h")==0)
		{
			goto ERRORU;
		}

		if (strcmp(argv[1],"-o")==0)
		{
			if (argv[2] != NULL)
			{
				ofile = argv[2];
				ofilef = 1; //write to file flag

			} else {
				goto ERRORU;
			}

		
			if (argv[3] != NULL)
			{
				//LIST + OUTPUTFILE 
				if (strcmp(argv[3],"-l")==0)  
				{ 
					if (argv[4] != NULL)
					{
						for (int i = 4; i < argc; i++)
								{
								//START
									/*
									if ( access( argv[i], F_OK) != -1)
									{
										parseFile(argv[i]);
									} else {
										goto ERROR;
									}
									*/
								//END
								//start - access function verify file exists - from unistd.h
							if ( access(argv[i], F_OK) != -1)
							{
								
								struct stat st;
								stat(argv[i], &st);
								char sbuf[10];
								double size = st.st_size; 
								
								if (size >= MAXFILES)
								{
									fprintf(stderr, "File too big (%s). Keep it under 500MB\n", readable_fs(size, sbuf));
									goto ERROR; 
								} /* else {
									fprintf(stderr, "File Name:%s Size:%s\n", argv[i], readable_fs(size, sbuf)  );
								} */


								/*
								The magic_load() function must be used to load the colon separated list
						        of database files passed in as filename, or NULL for the default data‐
						        base file before any magic queries can performed.
								*/
		 						 magic_load(myt,NULL);

								 if (strcmp(magic_file(myt,argv[i]),"application/gzip") == 0)
								 {
									if ( (access("/bin/zcat", F_OK) != -1) || (access("/usr/bin/zcat", F_OK) != -1))
										{
										 	char gbuff[1024];
										 	char fbuff[1024];
										 	char rbuff[1024];

									 	/*
											Writing a decompressed file is not the best way to handle this situation
											however it is certainly easier then trying to do the in memory approach
									 	*/
										 	base = basename(argv[i]);
									 		snprintf(gbuff, sizeof(gbuff), "zcat %s >> /tmp/%s_%d",argv[i],base,ftoken);
										 	snprintf(fbuff, sizeof(fbuff), "/tmp/%s_%d",base,ftoken);

											fprintf(stdout, "Decompressing %s (application/gzip) to %s\n",base,fbuff);

											int retg=system(gbuff);

											if (retg == 0)
											{
												fprintf(stdout, "Parsing file %s\n",fbuff);
												parseFile(fbuff);

											}
											//file cleanup 
											snprintf(rbuff, sizeof(rbuff), "rm -f /tmp/%s_%d",base,ftoken);
											system(rbuff);
									 	} else {
									 		fprintf(stderr, "File Decompression Failure (zcat tool missing)\n");
									 		goto ERROR;
									 	}	 	

							
								 
								 } else if (strcmp(magic_file(myt,argv[i]),"application/zip") == 0) {
							
									 if ( (access( "/bin/unzip", F_OK) != -1) || (access( "/usr/bin/unzip", F_OK) != -1))
										{
											char gbuff[1024];
										 	char fbuff[1024];
										 	char rbuff[1024];
										 	char dbuff[1024]; //tmp directory name buffer				

										 	base = basename(argv[i]);
										 	snprintf(gbuff, sizeof(gbuff), "unzip %s -d /tmp/%s_%d",argv[i],base,ftoken);
									 		snprintf(fbuff, sizeof(fbuff), "/tmp/%s_%d",base,ftoken);


									 		fprintf(stdout, "Unpacking %s (application/zip) to %s\n",base,fbuff);
									 		int retg=system(gbuff);

											if (retg == 0)
											{
												  DIR  *d;
												  struct dirent *dir;
												  d = opendir(fbuff);

													 if (d)
													  {
													    while ((dir = readdir(d)) != NULL)
													    {
													      snprintf(dbuff, sizeof(dbuff), "/tmp/%s_%d/%s",base,ftoken,dir->d_name);


													      /*
														   Supported files in the archive can be plain/text or text/html. 
													      */

													      if ((strcmp(magic_file(myt,dbuff),"text/html") == 0))
													      {
													      		fprintf(stdout, "Parsing file %s\n",dbuff);
																parseFile(dbuff);

													      } else if (strcmp(magic_file(myt,dbuff),"text/plain") == 0) 
													      {
													      		fprintf(stdout, "Parsing file %s\n",dbuff);
													      		parseFile(dbuff);

													      } else {

													      		continue; //skip file. No need for warning. 
													      }

													      memset(dbuff, 0, sizeof dbuff);
													    }

													    closedir(d);
													  }
											//directory cleanup 
											snprintf(rbuff, sizeof(rbuff), "rm -rf /tmp/%s_%d",base,ftoken);
											system(rbuff);	
											}

										} else {
									 		fprintf(stderr, "File Unpacking Failure (unzip tool missing)\n");
									 		goto ERROR;									
										}


								 } else if (strcmp(magic_file(myt,argv[i]),"text/html") == 0) {
								 	fprintf(stdout, "Parsing file %s\n",argv[i]);
								 	parseFile(argv[i]);

								 } else if (strcmp(magic_file(myt,argv[i]),"text/plain") == 0) {
								 	fprintf(stdout, "Parsing file %s\n",argv[i]);
								 	parseFile(argv[i]);
								 } else {
								 	printf("File type not supported\n");
								 }
								

							} else {
								goto ERRORU;
						    }
									




						 }

					} else {
						goto ERRORU; 
					} 


				} else { /*Single file (or glob) + output*/  
	

				struct stat st;
				char sbuf[10];
				double size = st.st_size; 
							
				 //glob detection 
				 for (i = 3; i < argc; i++) {
				    gret = glob(argv[i], GLOB_APPEND, NULL, &results);
				  }					


				  if (gret == 0)
				  {
					 	/* 
					 	glob detected - Deal with each file in the glob list. 
						*/

					 	char globbuf[LINESIZE];

					    for (i = 0; i < results.gl_pathc; i++)
					    {
					    	strcpy(globbuf,results.gl_pathv[i]);

					    	stat(globbuf, &st);

							if ( access( globbuf, F_OK) != -1)
								{
									//Now that we know the file exists, lets verify that it's not super huge
						
									if (size >= MAXFILES)
									{
										fprintf(stderr, "File too big (%s). Keep it under 500MB\n", readable_fs(size, sbuf));
										goto ERROR; 
									}
									
									 magic_load(myt,NULL);

									 if (strcmp(magic_file(myt,globbuf),"application/gzip") == 0)
									 {

										if ( (access("/bin/zcat", F_OK) != -1) || (access("/usr/bin/zcat", F_OK) != -1))
										{
										 	char gbuff[1024];
										 	char fbuff[1024];
										 	char rbuff[1024];

					
										 	base = basename(globbuf);

									 		snprintf(gbuff, sizeof(gbuff), "zcat %s >> /tmp/%s_%d",globbuf,base,ftoken);
										 	snprintf(fbuff, sizeof(fbuff), "/tmp/%s_%d",base,ftoken);

											fprintf(stdout, "Decompressing %s (application/gzip) to %s\n",base,fbuff);

											int retg=system(gbuff);

											if (retg == 0)
											{
												fprintf(stdout, "Parsing file %s\n",fbuff);
												parseFile(fbuff);

											}
											//file cleanup 
											snprintf(rbuff, sizeof(rbuff), "rm -f /tmp/%s_%d",base,ftoken);
											system(rbuff);
									 	} else {
									 		fprintf(stderr, "File Decompression Failure (zcat tool missing)\n");
									 		goto ERROR;
									 	}
									 
									 } else if (strcmp(magic_file(myt,globbuf),"application/zip") == 0) {
					
									 		if ( (access( "/bin/unzip", F_OK) != -1) || (access( "/usr/bin/unzip", F_OK) != -1))
											{
												char gbuff[1024];
											 	char fbuff[1024];
											 	char rbuff[1024];
											 	char dbuff[1024]; //tmp directory name buffer				

											 	base = basename(globbuf);
											 	snprintf(gbuff, sizeof(gbuff), "unzip %s -d /tmp/%s_%d",globbuf,base,ftoken);
										 		snprintf(fbuff, sizeof(fbuff), "/tmp/%s_%d",base,ftoken);

										 		fprintf(stdout, "Unpacking %s (application/zip) to %s\n",base,fbuff);
										 		int retg=system(gbuff);

												if (retg == 0)
												{
													  DIR  *d;
													  struct dirent *dir;
													  d = opendir(fbuff);

														 if (d)
														  {
														    while ((dir = readdir(d)) != NULL)
														    {
														      snprintf(dbuff, sizeof(dbuff), "/tmp/%s_%d/%s",base,ftoken,dir->d_name);

														      /*
															   Supported files in the archive can be plain/text or text/html. 
														      */

														      if ((strcmp(magic_file(myt,dbuff),"text/html") == 0))
														      {
														      		fprintf(stdout, "Parsing file %s\n",dbuff);
																	parseFile(dbuff);

														      } else if (strcmp(magic_file(myt,dbuff),"text/plain") == 0) 
														      {
														      		fprintf(stdout, "Parsing file %s\n",dbuff);
														      		parseFile(dbuff);

														      } else {

														      		continue; //skip file. No need for warning. 
														      }

														      memset(dbuff, 0, sizeof dbuff);
														    }

														    closedir(d);
														  }
												//directory cleanup 
												snprintf(rbuff, sizeof(rbuff), "rm -rf /tmp/%s_%d",base,ftoken);
												system(rbuff);	
												}

											} else {
										 		fprintf(stderr, "File Unpacking Failure (unzip tool missing)\n");
										 		goto ERROR;									
											}
									 	

									 } else if (strcmp(magic_file(myt,globbuf),"text/html") == 0) {
									 	fprintf(stdout, "Parsing file %s\n",globbuf);
									 	parseFile(globbuf);

									 } else if (strcmp(magic_file(myt,globbuf),"text/plain") == 0) {
									 	fprintf(stdout, "Parsing file %s\n",globbuf);
									 	parseFile(globbuf);
									 } else {
									 	printf("File type not supported\n");
									 }

								} else {
									goto ERRORU;
							    } 		

					    }
						//free glob memory 
					   // globfree(& results);

				  } else {
				  	//Some glob error occured. 
				  	//globfree(& results);
				  	goto ERRORU;
				  }



					
				}

			} else {
				goto ERRORU; 
			}

		} else { /*List + No Output */

			if (strcmp(argv[1],"-l")==0)	
			{
				if (argv[2] != NULL)
				{
					for (int i = 2; i < argc; i++)
					{
						//start - access function verify file exists - from unistd.h
						if ( access(argv[i], F_OK) != -1)
						{
							
							struct stat st;
							stat(argv[i], &st);
							char sbuf[10];
							double size = st.st_size; 
							
							if (size >= MAXFILES)
							{
								fprintf(stderr, "File too big (%s). Keep it under 500MB\n", readable_fs(size, sbuf));
								goto ERROR; 
							} /* else {
								fprintf(stderr, "File Name:%s Size:%s\n", argv[i], readable_fs(size, sbuf)  );
							} */


							/*
							The magic_load() function must be used to load the colon separated list
					        of database files passed in as filename, or NULL for the default data‐
					        base file before any magic queries can performed.
							*/
	 						 magic_load(myt,NULL);

							 if (strcmp(magic_file(myt,argv[i]),"application/gzip") == 0)
							 {
								if ( (access("/bin/zcat", F_OK) != -1) || (access("/usr/bin/zcat", F_OK) != -1))
									{
									 	char gbuff[1024];
									 	char fbuff[1024];
									 	char rbuff[1024];

								 	/*
										Writing a decompressed file is not the best way to handle this situation
										however it is certainly easier then trying to do the in memory approach

								 	*/
									 	base = basename(argv[i]);
								 		snprintf(gbuff, sizeof(gbuff), "zcat %s >> /tmp/%s_%d",argv[i],base,ftoken);
									 	snprintf(fbuff, sizeof(fbuff), "/tmp/%s_%d",base,ftoken);

										fprintf(stdout, "Decompressing %s (application/gzip) to %s\n",base,fbuff);

										int retg=system(gbuff);

										if (retg == 0)
										{
											fprintf(stdout, "Parsing file %s\n",fbuff);
											parseFile(fbuff);

										}
										//file cleanup 
										snprintf(rbuff, sizeof(rbuff), "rm -f /tmp/%s_%d",base,ftoken);
										system(rbuff);
								 	} else {
								 		fprintf(stderr, "File Decompression Failure (zcat tool missing)\n");
								 		goto ERROR;
								 	}	 	

							 } else if (strcmp(magic_file(myt,argv[i]),"application/zip") == 0) {
						
								 if ( (access( "/bin/unzip", F_OK) != -1) || (access( "/usr/bin/unzip", F_OK) != -1))
									{
										char gbuff[1024];
									 	char fbuff[1024];
									 	char rbuff[1024];
									 	char dbuff[1024]; //tmp directory name buffer				

									 	base = basename(argv[i]);
									 	snprintf(gbuff, sizeof(gbuff), "unzip %s -d /tmp/%s_%d",argv[i],base,ftoken);
								 		snprintf(fbuff, sizeof(fbuff), "/tmp/%s_%d",base,ftoken);

								 		fprintf(stdout, "Unpacking %s (application/zip) to %s\n",base,fbuff);
								 		int retg=system(gbuff);

										if (retg == 0)
										{
											  DIR  *d;
											  struct dirent *dir;
											  d = opendir(fbuff);

												 if (d)
												  {
												    while ((dir = readdir(d)) != NULL)
												    {
												      snprintf(dbuff, sizeof(dbuff), "/tmp/%s_%d/%s",base,ftoken,dir->d_name);


												      /*
													   Supported files in the archive can be plain/text or text/html. 
												      */

												      if ((strcmp(magic_file(myt,dbuff),"text/html") == 0))
												      {
												      		fprintf(stdout, "Parsing file %s\n",dbuff);
												      		parseFile(dbuff);

												      } else if (strcmp(magic_file(myt,dbuff),"text/plain") == 0) 
												      {
												      		fprintf(stdout, "Parsing file %s\n",dbuff);
												      		parseFile(dbuff);

												      } else {

												      		continue; //skip file. No need for warning. 
												      }

												      memset(dbuff, 0, sizeof dbuff);
												    }

												    closedir(d);
												  }
										//directory cleanup 
										snprintf(rbuff, sizeof(rbuff), "rm -rf /tmp/%s_%d",base,ftoken);
										system(rbuff);	
										}

									} else {
								 		fprintf(stderr, "File Unpacking Failure (unzip tool missing)\n");
								 		goto ERROR;									
									}


							 } else if (strcmp(magic_file(myt,argv[i]),"text/html") == 0) {
							 	fprintf(stdout, "Parsing file %s\n",argv[i]);
							 	parseFile(argv[i]);

							 } else if (strcmp(magic_file(myt,argv[i]),"text/plain") == 0) {
							 	fprintf(stdout, "Parsing file %s\n",argv[i]);
							 	parseFile(argv[i]);
							 } else {
							 	printf("File type not supported\n");
							 }
							

						} else {
							goto ERRORU;
					    }


					 }
				} else {
					goto ERROR;
				}

			} else { /*Single File (or glob) + No Output*/ 

			    struct stat st;
				char sbuf[10];
				double size = st.st_size; 
							
				 //glob detection 
				 for (i = 1; i < argc; i++) {
				    //flags |= (i > 1 ? GLOB_APPEND : 0);
				    gret = glob(argv[i], GLOB_APPEND, NULL, &results);
				  }					


				  if (gret == 0)
				  {
					 	/* 
					 	glob detected - Deal with each file in the glob list. 
						*/

					 	char globbuf[LINESIZE];

					    for (i = 0; i < results.gl_pathc; i++)
					    {

					    	strcpy(globbuf,results.gl_pathv[i]);

					    	stat(globbuf, &st);

							if ( access( globbuf, F_OK) != -1)
								{
									//Now that we know the file exists, lets verify that it's not super huge
						
									if (size >= MAXFILES)
									{
										fprintf(stderr, "File too big (%s). Keep it under 500MB\n", readable_fs(size, sbuf));
										goto ERROR; 
									}
									
									 magic_load(myt,NULL);

									 if (strcmp(magic_file(myt,globbuf),"application/gzip") == 0)
									 {

										if ( (access("/bin/zcat", F_OK) != -1) || (access("/usr/bin/zcat", F_OK) != -1))
										{
										 	char gbuff[1024];
										 	char fbuff[1024];
										 	char rbuff[1024];

					
										 	base = basename(globbuf);

									 		snprintf(gbuff, sizeof(gbuff), "zcat %s >> /tmp/%s_%d",globbuf,base,ftoken);
										 	snprintf(fbuff, sizeof(fbuff), "/tmp/%s_%d",base,ftoken);

											fprintf(stdout, "Decompressing %s (application/gzip) to %s\n",base,fbuff);

											int retg=system(gbuff);

											if (retg == 0)
											{
												parseFile(fbuff);

											}
											//file cleanup 
											snprintf(rbuff, sizeof(rbuff), "rm -f /tmp/%s_%d",base,ftoken);
											system(rbuff);
									 	} else {
									 		fprintf(stderr, "File Decompression Failure (zcat tool missing)\n");
									 		goto ERROR;
									 	}
									 
									 } else if (strcmp(magic_file(myt,globbuf),"application/zip") == 0) {
					
									 		if ( (access( "/bin/unzip", F_OK) != -1) || (access( "/usr/bin/unzip", F_OK) != -1))
											{
												char gbuff[1024];
											 	char fbuff[1024];
											 	char rbuff[1024];
											 	char dbuff[1024]; //tmp directory name buffer				

											 	base = basename(globbuf);
											 	snprintf(gbuff, sizeof(gbuff), "unzip %s -d /tmp/%s_%d",globbuf,base,ftoken);
										 		snprintf(fbuff, sizeof(fbuff), "/tmp/%s_%d",base,ftoken);

										 		fprintf(stdout, "Unpacking %s (application/zip) to %s\n",base,fbuff);
										 		int retg=system(gbuff);

												if (retg == 0)
												{
													  DIR  *d;
													  struct dirent *dir;
													  d = opendir(fbuff);

														 if (d)
														  {
														    while ((dir = readdir(d)) != NULL)
														    {
														      snprintf(dbuff, sizeof(dbuff), "/tmp/%s_%d/%s",base,ftoken,dir->d_name);

														      /*
															   Supported files in the archive can be plain/text or text/html. 
														      */

														      if ((strcmp(magic_file(myt,dbuff),"text/html") == 0))
														      {
														      		fprintf(stdout, "Parsing file %s\n",dbuff);
																	parseFile(dbuff);

														      } else if (strcmp(magic_file(myt,dbuff),"text/plain") == 0) 
														      {
														      		fprintf(stdout, "Parsing file %s\n",dbuff);
														      		parseFile(dbuff);

														      } else {

														      		continue; //skip file. No need for warning. 
														      }

														      memset(dbuff, 0, sizeof dbuff);
														    }

														    closedir(d);
														  }
												//directory cleanup 
												snprintf(rbuff, sizeof(rbuff), "rm -rf /tmp/%s_%d",base,ftoken);
												system(rbuff);	
												}

											} else {
										 		fprintf(stderr, "File Unpacking Failure (unzip tool missing)\n");
										 		goto ERROR;									
											}
									 	

									 } else if (strcmp(magic_file(myt,globbuf),"text/html") == 0) {
									 	fprintf(stdout, "Parsing file %s\n",globbuf);
									 	parseFile(globbuf);

									 } else if (strcmp(magic_file(myt,globbuf),"text/plain") == 0) {
									 	fprintf(stdout, "Parsing file %s\n",globbuf);
									 	parseFile(globbuf);
									 } else {
									 	printf("File type not supported\n");
									 }

								} else {
									goto ERRORU;
							    } 		

					    }
						//free glob memory 
					  //  globfree(& results);

				  } else {
				  	//Some glob error occured. 
				  	//globfree(& results);
				  	goto ERRORU;
				  }


			}  
		}

	}


debug_elements();

//sort linked list via merge sort agorithm 
	head = mergeSort(head);

//print elements 

 	if (ofilef == 1)
 	{
 		fprintf(stdout, "Writting Output to %s\n",ofile);
 		pret = print_elementFile(ofile);
 		if (pret)
 		{
 			fprintf(stderr, "Error writing to %s\n", ofile);
 		}

 	} else {
 		print_elements();
 	}

//delete linked list, and free memory 
	clean();		 
	return 0;



//goto statement 
ERRORU: 
	usage();
	goto ERROR; 

ERROR:
	/*
	magic_close() function closes the magic(4) database and deallocates any resources used.
	*/
	magic_close(myt);
	clean();
	exit(1);

}





