#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#include "config.h"
#include "libtelnet.h"
#include "item.h"
#include "value.h"
#include "test_assert.h"
#include "runtime_context.h"

CONFIG_t config;
const char *test_harness_current_suite(void){return "network";} const char *test_harness_current_test(void){return "network";} void test_harness_failf(const char *file,int line_no,const char *fmt,...){fprintf(stderr,"fail %s:%d %s\n",file,line_no,fmt); exit(1);}

typedef void (*test_fn_t)(void);
typedef struct { const char *name; test_fn_t fn; } test_case_t;

static int fail_malloc_after = -1, malloc_calls, uv_close_calls;
static uv_close_cb last_close_cb; static uv_handle_t *last_close_handle; static uv_write_cb last_write_cb; static uv_write_t *last_write_req;
static int stub_uv_write_result, stub_uv_accept_result, stub_uv_read_start_result, stub_uv_tcp_init_result, stub_uv_tcp_getpeername_result, stub_uv_ip_name_result, stub_telnet_init_fail;
static void reset_faults(void){fail_malloc_after=-1;malloc_calls=uv_close_calls=0;last_close_cb=NULL;last_close_handle=NULL;last_write_cb=NULL;last_write_req=NULL;stub_uv_write_result=stub_uv_accept_result=stub_uv_read_start_result=stub_uv_tcp_init_result=stub_uv_tcp_getpeername_result=stub_uv_ip_name_result=stub_telnet_init_fail=0;}
static void *test_malloc(size_t n){if(fail_malloc_after>=0&&malloc_calls++>=fail_malloc_after)return NULL;return malloc(n);} static void *test_calloc(size_t n,size_t s){if(fail_malloc_after>=0&&malloc_calls++>=fail_malloc_after)return NULL;return calloc(n,s);} static void *test_realloc(void*p,size_t n){if(fail_malloc_after>=0&&malloc_calls++>=fail_malloc_after)return NULL;return realloc(p,n);} static char *test_strdup(const char*s){if(fail_malloc_after>=0&&malloc_calls++>=fail_malloc_after)return NULL;return strdup(s);} 
#define malloc test_malloc
#define calloc test_calloc
#define realloc test_realloc
#define strdup test_strdup
#define uv_write test_uv_write
#define uv_close test_uv_close
#define uv_is_closing test_uv_is_closing
#define uv_accept test_uv_accept
#define uv_read_start test_uv_read_start
#define uv_tcp_init test_uv_tcp_init
#define uv_tcp_getpeername test_uv_tcp_getpeername
#define uv_ip_name test_uv_ip_name
#define uv_try_write test_uv_try_write
#define telnet_init test_telnet_init
#define telnet_free test_telnet_free
#define telnet_printf test_telnet_printf
#define telnet_recv test_telnet_recv
#define static

void logerr(const char *msg, ...){(void)msg;}
void logmsg(const char *msg, ...){(void)msg;}
void runtime_context_init(RuntimeContext *ctx, VM_t *vm){(void)ctx;(void)vm;}
VALUE_t interpret(RuntimeContext *ctx, ITEM_t *item){(void)ctx;(void)item; VALUE_t v={VALUE_nil,{0}}; return v;}
ITEM_t *find_item(ITEM_t *root, const char *name){(void)root;(void)name; return (ITEM_t *)1;}
void reset_stack(STACK_t *stack){(void)stack;}
int test_uv_write(uv_write_t *req, uv_stream_t *h, const uv_buf_t bufs[], unsigned int nbufs, uv_write_cb cb){(void)h;(void)bufs;(void)nbufs;last_write_req=req;last_write_cb=cb;return stub_uv_write_result;} void test_uv_close(uv_handle_t*h,uv_close_cb cb){uv_close_calls++;last_close_handle=h;last_close_cb=cb;} int test_uv_is_closing(const uv_handle_t*h){(void)h;return 0;} int test_uv_accept(uv_stream_t*s,uv_stream_t*c){(void)s;(void)c;return stub_uv_accept_result;} int test_uv_read_start(uv_stream_t*s,uv_alloc_cb a,uv_read_cb r){(void)s;(void)a;(void)r;return stub_uv_read_start_result;} int test_uv_tcp_init(uv_loop_t*l,uv_tcp_t*h){(void)l;(void)h;return stub_uv_tcp_init_result;} int test_uv_tcp_getpeername(const uv_tcp_t*h,struct sockaddr*n,int*len){(void)h;(void)n;(void)len;return stub_uv_tcp_getpeername_result;} int test_uv_ip_name(const struct sockaddr*src,char*dst,size_t size){(void)src;snprintf(dst,size,"127.0.0.1");return stub_uv_ip_name_result;} int test_uv_try_write(uv_stream_t*h,const uv_buf_t bufs[],unsigned int nbufs){(void)h;(void)bufs;(void)nbufs;return 0;} telnet_t *test_telnet_init(const telnet_telopt_t*opts,telnet_event_handler_t eh,unsigned char flags,void*ud){(void)opts;(void)eh;(void)flags;(void)ud;return stub_telnet_init_fail?NULL:(telnet_t*)test_malloc(8);} void test_telnet_free(telnet_t*t){free(t);} void test_telnet_printf(telnet_t*t,const char*fmt,...){(void)t;(void)fmt;} void test_telnet_recv(telnet_t*t,const char*b,size_t s){(void)t;(void)b;(void)s;}
#include "../../src/net/network.c"
#undef static
static uv_stream_t *test_server_with_deps(NetworkRuntimeDeps *deps){static uv_tcp_t listener;static uv_loop_t loop;deps->loop=&loop;deps->listener=&listener;deps->lines=&line;deps->maxconns=config.maxconns;listener.data=deps;return (uv_stream_t *)&listener;}
static LINE_t *make_line(void){reset_faults();config.maxconns=2;line=calloc(config.maxconns,sizeof(*line));uv_tcp_t*h=calloc(1,sizeof(*h));LINE_t*lp=add_line(h);ASSERT_NOT_NULL(lp);lp->status=LINE_idle;return lp;} static void cleanup_lines(void){for(size_t i=0;i<config.maxconns;i++)if(line[i].status!=LINE_empty)destroy_line(&line[i]);free(line);line=NULL;memset(&config,0,sizeof(config));}
void test_append_input_lines_and_limits(void){LINE_t*lp=make_line();append_input(lp,"one\n",4);ASSERT_EQ_INT(LINE_data,lp->status);append_input(lp,"two\nthree\n",10);ASSERT_EQ_INT(14,lp->inbuf->buf.len);ASSERT_EQ_INT(0,lp->input_line_length);cleanup_lines();lp=make_line();append_input(lp,"partial",7);ASSERT_EQ_INT(LINE_idle,lp->status);ASSERT_EQ_INT(7,lp->input_line_length);cleanup_lines();lp=make_line();char*big=calloc(4097,1);memset(big,'a',4096);append_input(lp,big,4096);append_input(lp,"b",1);ASSERT_EQ_INT(LINE_disconnecting,lp->status);free(big);cleanup_lines();lp=make_line();char*chunk=calloc(4097,1);memset(chunk,'x',4095);chunk[4095]='\n';for(int i=0;i<16;i++)append_input(lp,chunk,4096);append_input(lp,"z\n",2);ASSERT_EQ_INT(LINE_disconnecting,lp->status);free(chunk);cleanup_lines();}
void test_get_input_cases(void){LINE_t*lp=make_line();append_input(lp,"missing",7);lp->status=LINE_data;ASSERT_TRUE(get_input(lp)==NULL);ASSERT_EQ_INT(LINE_idle,lp->status);cleanup_lines();lp=make_line();append_input(lp,"first\nsecond\n",13);char*s=get_input(lp);ASSERT_NOT_NULL(s);ASSERT_TRUE(strcmp(s,"first")==0);free(s);ASSERT_TRUE(strcmp(lp->inbuf->buf.base,"second\n")==0);ASSERT_EQ_INT(LINE_data,lp->status);cleanup_lines();lp=make_line();append_input(lp,"x\n",2);fail_malloc_after=malloc_calls;ASSERT_TRUE(get_input(lp)==NULL);ASSERT_EQ_INT(LINE_disconnecting,lp->status);cleanup_lines();}
void test_output_flush_limits_and_callback(void){LINE_t*lp=make_line();append_output(lp,"abc",3);flush_output(lp);ASSERT_TRUE(lp->output_write_in_flight);ASSERT_EQ_INT(3,lp->output_in_flight_length);last_write_cb(last_write_req,0);ASSERT_TRUE(!lp->output_write_in_flight);lp->output_write_in_flight=true;append_output(lp,"q",1);for(int i=0;i<129;i++)flush_output(lp);ASSERT_EQ_INT(LINE_disconnecting,lp->status);cleanup_lines();lp=make_line();char*big=calloc(65537,1);memset(big,'o',65536);append_output(lp,big,65536);ASSERT_EQ_INT(LINE_disconnecting,lp->status);free(big);cleanup_lines();}
void test_disconnect_waits_for_pending_output(void){LINE_t*lp=make_line();append_output(lp,"bye",3);request_line_disconnect(lp);ASSERT_EQ_INT(LINE_disconnecting,lp->status);ASSERT_TRUE(lp->output_write_in_flight);ASSERT_EQ_INT(0,uv_close_calls);last_write_cb(last_write_req,0);ASSERT_EQ_INT(1,uv_close_calls);cleanup_lines();}
void test_on_new_connection_rejections_and_close_ownership(void){NetworkRuntimeDeps deps;reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));on_new_connection(NULL,0);ASSERT_EQ_INT(0,uv_close_calls);free(line);line=NULL;reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));line[0].status=LINE_idle;on_new_connection(test_server_with_deps(&deps),0);ASSERT_EQ_INT(1,uv_close_calls);last_close_cb(last_close_handle);free(line);line=NULL;reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));fail_malloc_after=0;on_new_connection(test_server_with_deps(&deps),0);ASSERT_EQ_INT(0,uv_close_calls);free(line);line=NULL;reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));stub_telnet_init_fail=1;on_new_connection(test_server_with_deps(&deps),0);ASSERT_EQ_INT(1,uv_close_calls);cleanup_lines();reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));stub_uv_read_start_result=UV_EIO;on_new_connection(test_server_with_deps(&deps),0);ASSERT_EQ_INT(1,uv_close_calls);cleanup_lines();reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));stub_uv_accept_result=UV_ECONNRESET;on_new_connection(test_server_with_deps(&deps),0);ASSERT_EQ_INT(1,uv_close_calls);last_close_cb(last_close_handle);free(line);line=NULL;reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));uv_tcp_t*orphan=calloc(1,sizeof(*orphan));client_on_close((uv_handle_t*)orphan);free(line);line=NULL;}
void test_adversarial_long_stream_without_newline(void){LINE_t*lp=make_line();char chunk[1024];memset(chunk,'a',sizeof(chunk));for(int i=0;i<8&&lp->status!=LINE_disconnecting;i++)append_input(lp,chunk,sizeof(chunk));ASSERT_EQ_INT(LINE_disconnecting,lp->status);ASSERT_TRUE(lp->inbuf->buf.len<=65536);cleanup_lines();}
static const test_case_t tests[]={{"append_input_lines_and_limits",test_append_input_lines_and_limits},{"get_input_cases",test_get_input_cases},{"output_flush_limits_and_callback",test_output_flush_limits_and_callback},{"disconnect_waits_for_pending_output",test_disconnect_waits_for_pending_output},{"on_new_connection_rejections_and_close_ownership",test_on_new_connection_rejections_and_close_ownership},{"adversarial_long_stream_without_newline",test_adversarial_long_stream_without_newline}};int main(void){for(size_t i=0;i<sizeof(tests)/sizeof(tests[0]);i++){printf("[network][RUN] %s\n",tests[i].name);tests[i].fn();printf("[network][PASS] %s\n",tests[i].name);}return 0;}
