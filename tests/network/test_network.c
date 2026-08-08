#include "item.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#include "config.h"
#include "libtelnet.h"
#include "item.h"
#include "log.h"
#include "value.h"
#include "test_assert.h"
#include "runtime_context.h"

CONFIG_t config; static size_t current_test_index, current_test_total; static const char *current_test_name = "<startup>";
const char *test_harness_current_suite(void){return "network";} const char *test_harness_current_test(void){return current_test_name;} void test_harness_failf(const char *file,int line_no,const char *fmt,...){va_list ap;fprintf(stderr,"[network][FAIL] test=%s at %s:%d: ",current_test_name,file,line_no);va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);fprintf(stderr,"\n[network] totals: ran=%zu passed=%zu failed=1 skipped=%zu status=FAILURE\n",current_test_index,current_test_index?current_test_index-1:0,current_test_total-current_test_index);exit(1);}

typedef void (*test_fn_t)(void);
typedef struct { const char *name; test_fn_t fn; } test_case_t;

static int fail_malloc_after = -1, malloc_calls, uv_close_calls;
static uv_close_cb last_close_cb; static uv_handle_t *last_close_handle; static uv_write_cb last_write_cb; static uv_write_t *last_write_req;
static int stub_uv_write_result, stub_uv_accept_result, stub_uv_read_start_result, stub_uv_tcp_init_result, stub_uv_tcp_getpeername_result, stub_uv_ip_name_result, stub_telnet_init_fail;
static int input_interpret_calls;
static uv_timer_t *input_timer_to_stop, *input_timer_watchdog;
static bool input_timer_watchdog_fired;
static VALUE_e input_result_type = VALUE_nil;
static int input_value_free_calls, input_string_free_calls;
static int input_live_result_strings;
static char *input_tracked_result;
static void reset_faults(void){fail_malloc_after=-1;malloc_calls=uv_close_calls=0;last_close_cb=NULL;last_close_handle=NULL;last_write_cb=NULL;last_write_req=NULL;stub_uv_write_result=stub_uv_accept_result=stub_uv_read_start_result=stub_uv_tcp_init_result=stub_uv_tcp_getpeername_result=stub_uv_ip_name_result=stub_telnet_init_fail=0;}
static void *test_malloc(size_t n){if(fail_malloc_after>=0&&malloc_calls++>=fail_malloc_after)return NULL;return malloc(n);} static void *test_calloc(size_t n,size_t s){if(fail_malloc_after>=0&&malloc_calls++>=fail_malloc_after)return NULL;return calloc(n,s);} static void *test_realloc(void*p,size_t n){if(fail_malloc_after>=0&&malloc_calls++>=fail_malloc_after)return NULL;return realloc(p,n);} static char *test_strdup(const char*s){if(fail_malloc_after>=0&&malloc_calls++>=fail_malloc_after)return NULL;return strdup(s);} 
static void complete_last_write(int status){if(last_write_cb&&last_write_req){uv_write_t *req=last_write_req;uv_write_cb cb=last_write_cb;last_write_req=NULL;last_write_cb=NULL;cb(req,status);}}
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
void logverbose(const char *msg, ...){(void)msg;}
void runtime_context_init(RuntimeContext *ctx, VM_t *vm){(void)ctx;(void)vm;}
VALUE_t interpret(RuntimeContext *ctx, ITEM_t *item){(void)ctx;(void)item; input_interpret_calls++; if (input_timer_to_stop && input_interpret_calls >= 3) { uv_timer_stop(input_timer_to_stop); uv_timer_stop(input_timer_watchdog); } VALUE_t v={input_result_type,{0}}; if (v.type == VALUE_str) { v.s = strdup("owned input result"); if (v.s) { input_tracked_result = v.s; input_live_result_strings++; } } return v;}
void value_free(VALUE_t *value){if (!value)return;input_value_free_calls++;if(value->type==VALUE_str&&value->s){if(value->s==input_tracked_result){input_tracked_result=NULL;input_live_result_strings--;}free(value->s);input_string_free_calls++;}value->type=VALUE_nil;value->i=0;}
ITEM_t *find_item(ITEM_t *root, const char *name){(void)root;(void)name; return (ITEM_t *)1;}
ITEM_t *itemstore_root(ITEMSTORE_t *store){(void)store; return (ITEM_t *)1;}
void reset_stack(STACK_t *stack){(void)stack;}
int test_uv_write(uv_write_t *req, uv_stream_t *h, const uv_buf_t bufs[], unsigned int nbufs, uv_write_cb cb){(void)h;(void)bufs;(void)nbufs;last_write_req=req;last_write_cb=cb;return stub_uv_write_result;} void test_uv_close(uv_handle_t*h,uv_close_cb cb){uv_close_calls++;last_close_handle=h;last_close_cb=cb;} int test_uv_is_closing(const uv_handle_t*h){(void)h;return 0;} int test_uv_accept(uv_stream_t*s,uv_stream_t*c){(void)s;(void)c;return stub_uv_accept_result;} int test_uv_read_start(uv_stream_t*s,uv_alloc_cb a,uv_read_cb r){(void)s;(void)a;(void)r;return stub_uv_read_start_result;} int test_uv_tcp_init(uv_loop_t*l,uv_tcp_t*h){(void)l;(void)h;return stub_uv_tcp_init_result;} int test_uv_tcp_getpeername(const uv_tcp_t*h,struct sockaddr*n,int*len){(void)h;(void)n;(void)len;return stub_uv_tcp_getpeername_result;} int test_uv_ip_name(const struct sockaddr*src,char*dst,size_t size){(void)src;snprintf(dst,size,"127.0.0.1");return stub_uv_ip_name_result;} int test_uv_try_write(uv_stream_t*h,const uv_buf_t bufs[],unsigned int nbufs){(void)h;(void)bufs;(void)nbufs;return 0;} telnet_t *test_telnet_init(const telnet_telopt_t*opts,telnet_event_handler_t eh,unsigned char flags,void*ud){(void)opts;(void)eh;(void)flags;(void)ud;return stub_telnet_init_fail?NULL:(telnet_t*)test_malloc(8);} void test_telnet_free(telnet_t*t){free(t);} void test_telnet_printf(telnet_t*t,const char*fmt,...){(void)t;(void)fmt;} void test_telnet_recv(telnet_t*t,const char*b,size_t s){(void)t;(void)b;(void)s;}
#include "../../src/net/network.c"
#undef static
#undef uv_close

static uv_stream_t *test_server_with_deps(NetworkRuntimeDeps *deps){static uv_tcp_t listener;static uv_loop_t loop;deps->loop=&loop;deps->listener=&listener;deps->lines=&line;deps->maxconns=config.maxconns;listener.data=deps;return (uv_stream_t *)&listener;}
static LINE_t *make_line(void){reset_faults();config.maxconns=2;line=calloc(config.maxconns,sizeof(*line));uv_tcp_t*h=calloc(1,sizeof(*h));LINE_t*lp=add_line(h);ASSERT_NOT_NULL(lp);lp->status=LINE_idle;return lp;} static void cleanup_lines(void){for(size_t i=0;i<config.maxconns;i++){LINE_t*lp=&line[i];if(lp->status==LINE_empty)continue;if(lp->output_write_in_flight&&last_write_req&&last_write_req->data==lp)complete_last_write(UV_ECANCELED);if(lp->line_handle&&!lp->close_completed){request_line_disconnect(lp);}if(lp->line_handle&&last_close_cb&&last_close_handle==(uv_handle_t*)lp->line_handle){last_close_cb(last_close_handle);last_close_handle=NULL;last_close_cb=NULL;}destroy_line(lp);}free(line);line=NULL;memset(&config,0,sizeof(config));}
void test_append_input_lines_and_limits(void){LINE_t*lp=make_line();append_input(lp,"one\n",4);ASSERT_EQ_INT(LINE_data,lp->status);append_input(lp,"two\nthree\n",10);ASSERT_EQ_INT(14,lp->inbuf->buf.len);ASSERT_EQ_INT(0,lp->input_line_length);cleanup_lines();lp=make_line();append_input(lp,"partial",7);ASSERT_EQ_INT(LINE_idle,lp->status);ASSERT_EQ_INT(7,lp->input_line_length);cleanup_lines();lp=make_line();char*big=calloc(4097,1);memset(big,'a',4096);append_input(lp,big,4096);append_input(lp,"b",1);ASSERT_EQ_INT(LINE_disconnecting,lp->status);free(big);cleanup_lines();lp=make_line();char*chunk=calloc(4097,1);memset(chunk,'x',4095);chunk[4095]='\n';for(int i=0;i<16;i++)append_input(lp,chunk,4096);append_input(lp,"z\n",2);ASSERT_EQ_INT(LINE_disconnecting,lp->status);free(chunk);cleanup_lines();}
void test_get_input_cases(void){LINE_t*lp=make_line();append_input(lp,"missing",7);lp->status=LINE_data;ASSERT_TRUE(get_input(lp)==NULL);ASSERT_EQ_INT(LINE_idle,lp->status);cleanup_lines();lp=make_line();append_input(lp,"first\nsecond\n",13);char*s=get_input(lp);ASSERT_NOT_NULL(s);ASSERT_TRUE(strcmp(s,"first")==0);free(s);ASSERT_TRUE(strcmp(lp->inbuf->buf.base,"second\n")==0);ASSERT_EQ_INT(LINE_data,lp->status);cleanup_lines();lp=make_line();append_input(lp,"x\n",2);fail_malloc_after=malloc_calls;ASSERT_TRUE(get_input(lp)==NULL);ASSERT_EQ_INT(LINE_disconnecting,lp->status);cleanup_lines();}
void test_output_flush_limits_and_callback(void){LINE_t*lp=make_line();append_output(lp,"abc",3);flush_output(lp);ASSERT_TRUE(lp->output_write_in_flight);ASSERT_EQ_INT(3,lp->output_in_flight_length);append_output(lp,"q",1);for(int i=0;i<129;i++)flush_output(lp);ASSERT_EQ_INT(LINE_disconnecting,lp->status);complete_last_write(UV_ECANCELED);cleanup_lines();lp=make_line();char*big=calloc(65537,1);memset(big,'o',65536);append_output(lp,big,65536);ASSERT_EQ_INT(LINE_disconnecting,lp->status);free(big);cleanup_lines();}
void test_disconnect_waits_for_pending_output(void){LINE_t*lp=make_line();append_output(lp,"bye",3);request_line_disconnect(lp);ASSERT_EQ_INT(LINE_disconnecting,lp->status);ASSERT_TRUE(lp->output_write_in_flight);ASSERT_EQ_INT(0,uv_close_calls);complete_last_write(0);ASSERT_EQ_INT(1,uv_close_calls);cleanup_lines();}
void test_line_lifecycle_states_and_reuse(void){reset_faults();config.maxconns=1;line=calloc(config.maxconns,sizeof(*line));ASSERT_TRUE(line_is_disconnected(&line[0]));ASSERT_TRUE(line_is_reusable(&line[0]));uv_tcp_t*h1=calloc(1,sizeof(*h1));LINE_t*lp=add_line(h1);ASSERT_NOT_NULL(lp);ASSERT_TRUE(line_is_active(lp));ASSERT_TRUE(!line_is_reusable(lp));lp->status=LINE_idle;request_line_disconnect(lp);ASSERT_TRUE(line_is_disconnect_pending(lp));ASSERT_EQ_INT(1,uv_close_calls);request_line_disconnect(lp);ASSERT_EQ_INT(1,uv_close_calls);append_output(lp,"ignored",7);ASSERT_EQ_INT(0,lp->outbuf->buf.len);last_close_cb(last_close_handle);ASSERT_TRUE(line_is_disconnect_pending(lp));destroy_line(lp);ASSERT_TRUE(line_is_disconnected(lp));ASSERT_TRUE(line_is_reusable(lp));uv_tcp_t*h2=calloc(1,sizeof(*h2));LINE_t*reused=add_line(h2);ASSERT_TRUE(reused==lp);ASSERT_TRUE(reused->line_handle==h2);ASSERT_TRUE(line_is_active(reused));cleanup_lines();}
void test_remote_disconnect_marks_line_before_close_callback(void){LINE_t*lp=make_line();uv_buf_t buf={calloc(1,1),1};client_read((uv_stream_t*)lp->line_handle,UV_EOF,&buf);ASSERT_TRUE(line_is_disconnect_pending(lp));ASSERT_EQ_INT(1,uv_close_calls);append_output(lp,"ignored",7);ASSERT_EQ_INT(0,lp->outbuf->buf.len);last_close_cb(last_close_handle);ASSERT_TRUE(line_is_disconnect_pending(lp));cleanup_lines();}
void test_disconnect_close_write_callback_orders(void){
  LINE_t*lp=make_line();
  append_output(lp,"pending",7); flush_output(lp);
  ASSERT_TRUE(lp->output_write_in_flight);
  request_line_disconnect(lp);
  ASSERT_EQ_INT(0,uv_close_calls);
  uv_buf_t eof_buf={calloc(1,1),1};
  client_read((uv_stream_t *)lp->line_handle,UV_EOF,&eof_buf);
  ASSERT_EQ_INT(1,uv_close_calls);
  last_close_cb(last_close_handle);
  ASSERT_TRUE(lp->close_completed);
  ASSERT_TRUE(lp->line_handle == NULL);
  ASSERT_NOT_NULL(lp->outbuf);
  complete_last_write(0);
  ASSERT_TRUE(lp->outbuf == NULL);
  ASSERT_TRUE(lp->inbuf == NULL);
  ASSERT_TRUE(lp->telnet == NULL);
  ASSERT_TRUE(line_is_disconnect_pending(lp));
  cleanup_lines();

  lp=make_line();
  append_output(lp,"pending",7); flush_output(lp);
  request_line_disconnect(lp);
  complete_last_write(0);
  ASSERT_EQ_INT(1,uv_close_calls);
  ASSERT_TRUE(lp->output_write_in_flight == false);
  last_close_cb(last_close_handle);
  ASSERT_TRUE(lp->line_handle == NULL);
  ASSERT_TRUE(lp->outbuf == NULL);
  ASSERT_TRUE(line_is_disconnect_pending(lp));
  cleanup_lines();
}
void test_destroy_line_does_not_release_live_transport(void){
  LINE_t*lp=make_line();
  telnet_t *telnet=lp->telnet;
  write_req_t *outbuf=lp->outbuf;
  destroy_line(lp);
  ASSERT_TRUE(lp->status == LINE_idle);
  ASSERT_TRUE(lp->line_handle != NULL);
  ASSERT_TRUE(lp->telnet == telnet);
  ASSERT_TRUE(lp->outbuf == outbuf);
  client_on_close((uv_handle_t *)lp->line_handle);
  destroy_line(lp);
  ASSERT_TRUE(line_is_disconnected(lp));
  cleanup_lines();
}
void test_on_new_connection_rejections_and_close_ownership(void){NetworkRuntimeDeps deps;reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));on_new_connection(NULL,0);ASSERT_EQ_INT(0,uv_close_calls);free(line);line=NULL;reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));line[0].status=LINE_idle;on_new_connection(test_server_with_deps(&deps),0);ASSERT_EQ_INT(1,uv_close_calls);last_close_cb(last_close_handle);free(line);line=NULL;reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));fail_malloc_after=0;on_new_connection(test_server_with_deps(&deps),0);ASSERT_EQ_INT(0,uv_close_calls);free(line);line=NULL;reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));stub_telnet_init_fail=1;on_new_connection(test_server_with_deps(&deps),0);ASSERT_EQ_INT(1,uv_close_calls);cleanup_lines();reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));stub_uv_read_start_result=UV_EIO;on_new_connection(test_server_with_deps(&deps),0);ASSERT_EQ_INT(1,uv_close_calls);cleanup_lines();reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));stub_uv_accept_result=UV_ECONNRESET;on_new_connection(test_server_with_deps(&deps),0);ASSERT_EQ_INT(1,uv_close_calls);last_close_cb(last_close_handle);free(line);line=NULL;reset_faults();config.maxconns=1;line=calloc(1,sizeof(*line));uv_tcp_t*orphan=calloc(1,sizeof(*orphan));client_on_close((uv_handle_t*)orphan);free(line);line=NULL;}
void test_adversarial_long_stream_without_newline(void){LINE_t*lp=make_line();char chunk[1024];memset(chunk,'a',sizeof(chunk));for(int i=0;i<8&&lp->status!=LINE_disconnecting;i++)append_input(lp,chunk,sizeof(chunk));ASSERT_EQ_INT(LINE_disconnecting,lp->status);ASSERT_TRUE(lp->inbuf->buf.len<=65536);cleanup_lines();}
void test_input_processor_releases_interpreter_results(void){
  uv_timer_t timer = {0};
  VM_t vm = {0};
  RuntimeContext ctx = {0};
  size_t maxconns = 0;
  ctx.vm = &vm;
  ctx.input_name = "input";
  ctx.maxconns = &maxconns;
  timer.data = &ctx;

  input_interpret_calls = 0;
  input_value_free_calls = 0;
  input_string_free_calls = 0;
  input_live_result_strings = 0;
  input_result_type = VALUE_str;
  input_processor(&timer);
  ASSERT_EQ_INT(1, input_interpret_calls);
  ASSERT_EQ_INT(1, input_value_free_calls);
  ASSERT_EQ_INT(1, input_string_free_calls);
  ASSERT_EQ_INT(0, input_live_result_strings);
  ASSERT_TRUE(input_tracked_result == NULL);

  input_value_free_calls = 0;
  input_string_free_calls = 0;
  input_result_type = VALUE_nil;
  input_processor(&timer);
  input_result_type = VALUE_int;
  input_processor(&timer);
  ASSERT_EQ_INT(2, input_value_free_calls);
  ASSERT_EQ_INT(0, input_string_free_calls);
  ASSERT_EQ_INT(0, input_live_result_strings);
  ASSERT_TRUE(input_tracked_result == NULL);
}
static void input_timer_watchdog_cb(uv_timer_t *timer) {
  input_timer_watchdog_fired = true;
  uv_timer_stop((uv_timer_t *)timer->data);
}

void test_input_processor_timer_is_nonblocking_and_sleepable(void) {
  uv_loop_t loop;
  uv_timer_t input_timer;
  uv_timer_t watchdog_timer;
  VM_t vm = {0};
  RuntimeContext ctx = {0};
  size_t maxconns = 0;

  ASSERT_EQ_INT(0, uv_loop_init(&loop));
  ASSERT_EQ_INT(0, uv_loop_configure(&loop, UV_METRICS_IDLE_TIME));
  ctx.vm = &vm;
  ctx.input_name = "input";
  ctx.maxconns = &maxconns;
  input_interpret_calls = 0;
  input_timer_watchdog_fired = false;

  ASSERT_EQ_INT(0, uv_timer_init(&loop, &input_timer));
  input_timer.data = &ctx;
  ASSERT_EQ_INT(0, uv_timer_init(&loop, &watchdog_timer));
  watchdog_timer.data = &input_timer;
  input_timer_to_stop = &input_timer;
  input_timer_watchdog = &watchdog_timer;
  ASSERT_EQ_INT(0, uv_timer_start(&input_timer, input_processor, 1, 10));
  ASSERT_EQ_INT(0, uv_timer_start(&watchdog_timer, input_timer_watchdog_cb, 1000, 0));
  ASSERT_EQ_INT(0, uv_run(&loop, UV_RUN_DEFAULT));
  input_timer_to_stop = NULL;
  input_timer_watchdog = NULL;

  ASSERT_EQ_INT(3, input_interpret_calls);
  ASSERT_TRUE(!input_timer_watchdog_fired);
  ASSERT_TRUE(uv_metrics_idle_time(&loop) > 0);
  uv_close((uv_handle_t *)&input_timer, NULL);
  uv_close((uv_handle_t *)&watchdog_timer, NULL);
  ASSERT_EQ_INT(0, uv_run(&loop, UV_RUN_DEFAULT));
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
}

static const test_case_t tests[]={{"append_input_lines_and_limits",test_append_input_lines_and_limits},{"get_input_cases",test_get_input_cases},{"output_flush_limits_and_callback",test_output_flush_limits_and_callback},{"disconnect_waits_for_pending_output",test_disconnect_waits_for_pending_output},{"line_lifecycle_states_and_reuse",test_line_lifecycle_states_and_reuse},{"remote_disconnect_marks_line_before_close_callback",test_remote_disconnect_marks_line_before_close_callback},{"disconnect_close_write_callback_orders",test_disconnect_close_write_callback_orders},{"destroy_line_does_not_release_live_transport",test_destroy_line_does_not_release_live_transport},{"on_new_connection_rejections_and_close_ownership",test_on_new_connection_rejections_and_close_ownership},{"adversarial_long_stream_without_newline",test_adversarial_long_stream_without_newline},{"input_processor_releases_interpreter_results",test_input_processor_releases_interpreter_results},{"input_processor_timer_is_nonblocking_and_sleepable",test_input_processor_timer_is_nonblocking_and_sleepable}};int main(void){size_t total=sizeof(tests)/sizeof(tests[0]);current_test_total=total;for(size_t i=0;i<total;i++){current_test_index=i+1;current_test_name=tests[i].name;tests[i].fn();}printf("[network] totals: ran=%zu passed=%zu failed=0 skipped=0 status=SUCCESS\n",total,total);return 0;}
