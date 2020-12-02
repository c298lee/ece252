#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <strings.h>
#include <sys/types.h>
#include <curl/curl.h>
#ifndef WIN31
#include <unistd.h>
#endif
#include <curl/multi.h>
#include <search.h>
#include <time.h>
#include <sys/time.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>

#define MAX_WAIT_MSECS 29*1000 /* Wait max. 30 seconds */

#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048575  /* 1024*1024 = 1M */
#define BUF_INC  524287   /* 1024*512  = 0.5M */

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  8
#define CT_HTML_LEN 8

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
	CURLM* curl;
} RECV_BUF;

typedef struct node {
    char url[256];
    struct node *next;
    struct node *previous;
}NODE;

int list_size = 0;
int max_pngs = 50;
int num_sessions = 1;
char logfile[256];
int CNT;
int num_pngs = 0;
char* keys[1250];
int num = 0;
NODE *head;
NODE *tail;
NODE *current;
double times[2];
struct timeval tv;

static size_t cb(char *d, size_t n, size_t l, void *p)
{
  /* take care of the data here, ignored in this example */
  (void)d;
  (void)p;
  return n;
}

size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);   
        char *q = realloc(p->buf, new_size);
    	 if (q == NULL) {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}


size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

#ifdef DEBUG1_
    //printf("%s", p_recv);
#endif /* DEBUG1_ */
    if (realsize > strlen(ECE252_HEADER) &&
	strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

   /* extract img sequence number */
	p->seq = atoi(p_recv + strlen(ECE252_HEADER));

    }
    return realsize;
}

static void init(CURLM *cm, char* url, RECV_BUF* buf)
{
  CURL *eh = curl_easy_init();
  buf->curl = eh;
  curl_easy_setopt(eh, CURLOPT_URL, url); 
  curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb_curl3); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(eh, CURLOPT_WRITEDATA, (void *)buf);

    /* register header call back function to process received header data */
    curl_easy_setopt(eh, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(eh, CURLOPT_HEADERDATA, (void *)buf);

    /* some servers requires a user-agent field */
    curl_easy_setopt(eh, CURLOPT_USERAGENT, "ece252 lab5 crawler");

    /* follow HTTP 3XX redirects */
    curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, 1L);
    /* continue to send authentication credentials when following locations */
    curl_easy_setopt(eh, CURLOPT_UNRESTRICTED_AUTH, 1L);
    /* max numbre of redirects to follow sets to 5 */
    curl_easy_setopt(eh, CURLOPT_MAXREDIRS, 5L);
    /* supports all built-in encodings */ 
    curl_easy_setopt(eh, CURLOPT_ACCEPT_ENCODING, "");

    /* Enable the cookie engine without reading any initial cookies */
    curl_easy_setopt(eh, CURLOPT_COOKIEFILE, "");
    /* allow whatever auth the proxy speaks */
    curl_easy_setopt(eh, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    /* allow whatever auth the server speaks */
    curl_easy_setopt(eh, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
  
  curl_multi_add_handle(cm, eh);
}

htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | \
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);

    if ( doc == NULL ) {
        //fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath)
{
	
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        //printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        //printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        //printf("No result\n");
        return NULL;
    }
    return result;
}

int find_http(char *buf, int size, int follow_relative_links, const char *base_url)
{

    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;
		
    if (buf == NULL) {
		printf("no buf xD\n");
			return 1;
    }

    doc = mem_getdoc(buf, size, base_url);

    result = getnodeset (doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i=0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if ( follow_relative_links ) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *) base_url);
                xmlFree(old);
            }
            if ( href != NULL && !strncmp((const char *)href, "http", 4) ) {	        	 write_frontier(href);
            }
            xmlFree(href);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return 0;
}
int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = NULL;
    
    if (ptr == NULL) {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
	return 2;
    }
    
    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1;              /* valid seq should be positive */
    return 0;
}

CURL *easy_handle_init(RECV_BUF *ptr, const char *url)
{
    CURL *curl_handle = NULL;

    if ( ptr == NULL || url == NULL) {
        return NULL;
    }

    /* init user defined call back function buffer */
    if ( recv_buf_init(ptr, BUF_SIZE) != 0 ) {
        return NULL;
    }
    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        return NULL;
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)ptr);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)ptr);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ece252 lab5 crawler");

    /* follow HTTP 3XX redirects */
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    /* continue to send authentication credentials when following locations */
    curl_easy_setopt(curl_handle, CURLOPT_UNRESTRICTED_AUTH, 1L);
    /* max numbre of redirects to follow sets to 5 */
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 5L);
    /* supports all built-in encodings */ 
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");

    /* Enable the cookie engine without reading any initial cookies */
    curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
    /* allow whatever auth the proxy speaks */
    curl_easy_setopt(curl_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    /* allow whatever auth the server speaks */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

    return curl_handle;
}

int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    int follow_relative_link = 1;
    char *url = NULL; 
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, url);
    return 0;
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    char fname[256];
    char *eurl = NULL;          /* effective URL */
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    int check = is_png(p_recv_buf->buf, p_recv_buf->size);
    if(check == 0){
        if(num_pngs<max_pngs){
            FILE *fp = NULL;
            fp = fopen("png_urls.txt", "a");
            fprintf(fp, "%s\n", eurl);
            fclose(fp);
            num_pngs++;
        }
        else{
            return NULL;
        }
    }
    sprintf(fname, "./output_%d.png", p_recv_buf->seq);
    return 0;
}

int is_png(uint8_t *buf, size_t n){
	if (buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47)
	{
			return 0;
	}
	return -1;
}

int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    CURLcode res;
    char fname[256];
    long response_code;

    res = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);

    if ( response_code >= 400 ) { 
		return 1;
    }

    char *ct = NULL;
    res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if ( res == CURLE_OK && ct != NULL ) {
    } else {
        return 2;
    }
    if ( strstr(ct, CT_HTML) ) {
        return process_html(curl_handle, p_recv_buf);
    } else if ( strstr(ct, CT_PNG) ) {
        return process_png(curl_handle, p_recv_buf);
    } 
    return 0;
}

void url_call(char *url){
    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_handle = easy_handle_init(&recv_buf, url);
    
    res = curl_easy_perform(curl_handle);

    if( res != CURLE_OK) {
        //broken links
        cleanup(curl_handle, &recv_buf);
        return;
    } 

    /* process the download data */
    process_data(curl_handle, &recv_buf);

    /* cleaning up */
    cleanup(curl_handle, &recv_buf);
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL) {
	return 1;
    }

    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

void cleanup(CURL *curl, RECV_BUF *ptr)
{
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        recv_buf_cleanup(ptr);
}

void write_frontier(char* url){
    NODE *n = (NODE*) malloc(sizeof(NODE));
    strcpy(n->url, url);

    list_size++;
    if(head == NULL){
        head = n;
        tail = n;
    }
    else{
        tail->next = n;
        n->previous = tail;
        tail = n;
        tail->next = NULL;
    }
}

const char* get_frontier(){
    char *url=malloc(256);
    list_size--;
    if(head!=NULL){
        strcpy(url, head->url);
        if(head->next !=NULL){
            NODE *temp = head;
            head = head->next;
            head->previous = NULL;
            free(temp);
        }
        else{
            free(head);
            head = NULL;
        }
    }
    return url;
}

int enter_visited(char* url){
    ENTRY e;
    ENTRY *exists;
    ENTRY *enter;
    e.key=strdup(url);
  
    exists = hsearch(e, FIND);  
    if(exists == NULL){
        enter = hsearch(e, ENTER);
		keys[num] = e.key;
        num++;
        if(strcmp(logfile, "") !=0){
            FILE *fp = NULL;
            fp = fopen(logfile, "a");
            fprintf(fp, "%s\n", url);
            fclose(fp);
        }
        return 0;
    }
    else{
		free(e.key);
        return -2;
    }
}

void clear_linkedlist(){
    NODE *temp = head;
    while (head) {
        head = head->next;
        free(temp);
        temp = head;
    }
	
	for (int i = 0; i < num; i++) {
		free(keys[i]);
	}
}

int main(int argc, char** argv)
{
    CURLM *cm=NULL;
    CURL *eh=NULL;
    CURLMsg *msg=NULL;
    CURLcode return_code=0;
    int still_running=0, i=0, msgs_left=0;
    int http_status_code;
	char* szUrl;
	char url[256];
    strcpy(logfile, "");
    remove("png_urls.txt");
    hcreate(1250);
    int arg=1;

    while(arg<argc){
        if(strcmp (argv[arg], "-t") == 0){
            arg++;
            num_sessions=atoi(argv[arg]);
        }
        else if(strcmp (argv[arg], "-m") == 0){
            arg++;
            max_pngs=atoi(argv[arg]);
        }
        else if(strcmp (argv[arg], "-v") == 0){
            arg++;
            sprintf(logfile, argv[arg]);
        }
        else{
            sprintf(url, argv[arg]);
            write_frontier(url);
        }
        arg++;
    }

	remove(logfile);

	RECV_BUF* bufs = (RECV_BUF*)malloc(sizeof(RECV_BUF)*num_sessions);
	for (int i = 0; i < num_sessions; i ++){
		recv_buf_init(&bufs[i], BUF_SIZE);
	}

    if (gettimeofday(&tv, NULL) != 0) {
            perror("gettimeofday");
            abort();
    }
    times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;
	curl_global_init(CURL_GLOBAL_ALL);

    cm = curl_multi_init();

    while (head != NULL) {
        CNT = num_sessions;
        if (CNT > num_sessions) {
            CNT = num_sessions;
        }

        for (i = 0; i < CNT && head != NULL; ++i) {
			int visited = -1;
			char *curr_url;
			while (visited != 0 && head != NULL) {
            	curr_url = get_frontier();
            	visited = enter_visited(curr_url);
			}
			if (visited == 0)
				init(cm, curr_url, &bufs[i]);
			if (curr_url != NULL)
				free(curr_url);
        }

        curl_multi_perform(cm, &still_running);

        do {
            int numfds=0;
            int res = curl_multi_wait(cm, NULL, 0, MAX_WAIT_MSECS, &numfds);
            if(res != CURLM_OK) {
                fprintf(stderr, "error: curl_multi_wait() returned %d\n", res);
                return EXIT_FAILURE;
            }
            curl_multi_perform(cm, &still_running);

        } while(still_running);


		while ((msg = curl_multi_info_read(cm, &msgs_left))) {
			if (msg->msg == CURLMSG_DONE) {
				eh = msg->easy_handle;
				RECV_BUF recv_buf;

				return_code = msg->data.result;
				if(return_code!=CURLE_OK) {
					continue;
				}


				int bufNum  = -1;
				for (int i = 0; i < num_sessions; i++){
					if (bufs[i].curl == eh){
						process_data(eh, &bufs[i]);
						bufNum = i;
					}
				}
				
				//curl_easy_getinfo(eh, CURLINFO_EFFECTIVE_URL, &szUrl);
				//char* arg = malloc(256);
				//arg = strdup(szUrl);
				//url_call(arg);
				//free(arg);
				
				curl_multi_remove_handle(cm, eh);
				curl_easy_cleanup(eh);
				if (bufNum != -1) {
					recv_buf_cleanup(&bufs[bufNum]);
					recv_buf_init(&bufs[bufNum], BUF_SIZE);
				}
			}
			else {
				fprintf(stderr, "error: after curl_multi_info_read(), CURLMsg=%d\n", msg->msg);
			}
		}
    }

	curl_multi_cleanup(cm);

    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
	printf("findpng3 execution time: %.6lf seconds\n",times[1]-times[0]);

    clear_linkedlist();
    hdestroy();

    return EXIT_SUCCESS;
}
