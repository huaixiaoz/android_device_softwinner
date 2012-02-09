/*
 * Copyright 2008, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "hardware_legacy/wifi.h"
#include "libwpa_client/wpa_ctrl.h"

#define LOG_TAG "WifiHW"
#include "cutils/log.h"
#include "cutils/memory.h"
#include "cutils/misc.h"
#include "cutils/properties.h"
#include "private/android_filesystem_config.h"
#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#endif

static struct wpa_ctrl *ctrl_conn;
static struct wpa_ctrl *monitor_conn;

extern int do_dhcp();
extern int ifc_init();
extern void ifc_close();
extern char *dhcp_lasterror();
extern void get_dhcp_info();
extern int init_module(void *, unsigned long, const char *);
extern int delete_module(const char *, unsigned int);

static char iface[PROPERTY_VALUE_MAX];
//  TODO: use new ANDROID_SOCKET mechanism, once support for multiple
// sockets is in

#if defined APM6xxx_SDIO_WIFI_USED
    #ifndef WIFI_DRIVER_MODULE_PATH
    #define WIFI_DRIVER_MODULE_PATH         "/drv/unifi_sdio.ko"
    #endif
    #ifndef WIFI_DRIVER_MODULE_NAME
    #define WIFI_DRIVER_MODULE_NAME         "unifi_sdio"
    #endif
    #ifndef WIFI_DRIVER_MODULE_ARG
    #define WIFI_DRIVER_MODULE_ARG          ""
    #endif
    
#elif defined AR6302_SDIO_WIFI_USED

    #ifndef WIFI_DRIVER_MODULE_PATH
    #define WIFI_DRIVER_MODULE_PATH         "/drv/ar6000.ko"
    #endif
    #ifndef WIFI_DRIVER_MODULE_NAME
    #define WIFI_DRIVER_MODULE_NAME         "ar6000"
    #endif
    #ifndef WIFI_DRIVER_MODULE_ARG
    #define WIFI_DRIVER_MODULE_ARG         "fwpath=/drv"
    #endif
    
#elif defined USI_BCM4329_SDIO_WIFI_USED

    #ifndef WIFI_DRIVER_MODULE_PATH
    #define WIFI_DRIVER_MODULE_PATH         "/drv/usi4329_dhd.ko"
    #endif
    #ifndef WIFI_DRIVER_MODULE_NAME
    #define WIFI_DRIVER_MODULE_NAME         "dhd"
    #endif
    #ifndef WIFI_DRIVER_MODULE_ARG
    #define WIFI_DRIVER_MODULE_ARG         "firmware_path=/drv/usi4329_fw.bin nvram_path=/drv/usi4329_nvram.txt"
    #endif
    
#elif defined SWBB23_SDIO_WIFI_USED

    #ifndef WIFI_DRIVER_MODULE_PATH
    #define WIFI_DRIVER_MODULE_PATH         "/drv/swbb23_dhd.ko"
    #endif
    #ifndef WIFI_DRIVER_MODULE_NAME
    #define WIFI_DRIVER_MODULE_NAME         "dhd"
    #endif
    #ifndef WIFI_DRIVER_MODULE_ARG
    #define WIFI_DRIVER_MODULE_ARG         "firmware_path=/drv/swbb23_fw.bin nvram_path=/drv/swbb23_nvram.txt"
    #endif
    
#elif defined NANO_SDIO_WIFI_USED 

    /* nano sdio wifi */
    #ifndef WIFI_DRIVER_MODULE_PATH
    #define WIFI_DRIVER_MODULE_PATH         "/drv/nano_ksdio.ko"
    #endif
    #ifndef WIFI_DRIVER_MODULE_NAME
    #define WIFI_DRIVER_MODULE_NAME         "nano_ksdio"
    #endif


    #ifndef WIFI_FIRMWARE_MODULE_PATH
    #define WIFI_FIRMWARE_MODULE_PATH         "/drv/nano_if.ko"
    #endif
    #ifndef WIFI_FIRMWARE_MODULE_NAME
    #define WIFI_FIRMWARE_MODULE_NAME         "nano_if"
    #endif
    #ifndef WIFI_FIRMWARE_MODULE_ARG
    #define WIFI_FIRMWARE_MODULE_ARG          "nrx_config=/drv"
    #endif

static const char FIRMWARE_MODULE_NAME[]  = WIFI_FIRMWARE_MODULE_NAME;
static const char FIRMWARE_MODULE_PATH[]  = WIFI_FIRMWARE_MODULE_PATH;
static const char FIRMWARE_MODULE_ARG[]   = WIFI_FIRMWARE_MODULE_ARG;
    
#else 
    /* rtl8192cu usb wifi */
    #ifdef RTL_USB_WIFI_USED
        #ifndef WIFI_DRIVER_MODULE_PATH
        #define WIFI_DRIVER_MODULE_PATH         "/drv/8192cu.ko"
        #endif
        #ifndef WIFI_DRIVER_MODULE_NAME
        #define WIFI_DRIVER_MODULE_NAME         "8192cu"
        #endif
    #else
        #ifndef WIFI_DRIVER_MODULE_PATH
        #define WIFI_DRIVER_MODULE_PATH         "/system/lib/modules/wlan.ko"
        #endif
        #ifndef WIFI_DRIVER_MODULE_NAME
        #define WIFI_DRIVER_MODULE_NAME         "wlan0"
        #endif
    #endif

#endif


#ifndef WIFI_DRIVER_MODULE_ARG
#define WIFI_DRIVER_MODULE_ARG          ""
#endif
#ifndef WIFI_FIRMWARE_LOADER
#define WIFI_FIRMWARE_LOADER		""
#endif
#define WIFI_TEST_INTERFACE		"sta"

#define WIFI_DRIVER_LOADER_DELAY	1000000



static const char IFACE_DIR[]           = "/data/system/wpa_supplicant";
static const char DRIVER_MODULE_NAME[]  = WIFI_DRIVER_MODULE_NAME;
static const char DRIVER_MODULE_TAG[]   = WIFI_DRIVER_MODULE_NAME " ";
static const char DRIVER_MODULE_PATH[]  = WIFI_DRIVER_MODULE_PATH;
static const char DRIVER_MODULE_ARG[]   = WIFI_DRIVER_MODULE_ARG;
static const char FIRMWARE_LOADER[]     = WIFI_FIRMWARE_LOADER;
static const char DRIVER_PROP_NAME[]    = "wlan.driver.status";
static const char SUPPLICANT_NAME[]     = "wpa_supplicant";
static const char SUPP_PROP_NAME[]      = "init.svc.wpa_supplicant";
static const char SUPP_CONFIG_TEMPLATE[]= "/system/etc/wifi/wpa_supplicant.conf";
static const char SUPP_CONFIG_FILE[]    = "/data/misc/wifi/wpa_supplicant.conf";
static const char MODULE_FILE[]         = "/proc/modules";



static int insmod(const char *filename, const char *args)
{
    void *module;
    unsigned int size;
    int ret;

    module = load_file(filename, &size);
    if (!module)
        return -1;

    ret = init_module(module, size, args);

    free(module);

    return ret;
}

static int rmmod(const char *modname)
{
    int ret = -1;
    int maxtry = 10;

    while (maxtry-- > 0) {
        ret = delete_module(modname, O_NONBLOCK | O_EXCL);
        if (ret < 0 && errno == EAGAIN)
            usleep(500000);
        else
            break;
    }

    if (ret != 0)
        LOGD("Unable to unload driver module \"%s\": %s\n",
             modname, strerror(errno));
    return ret;
}

int do_dhcp_request(int *ipaddr, int *gateway, int *mask,
                    int *dns1, int *dns2, int *server, int *lease) {
    /* For test driver, always report success */
    if (strcmp(iface, WIFI_TEST_INTERFACE) == 0)
        return 0;

    if (ifc_init() < 0)
        return -1;

    if (do_dhcp(iface) < 0) {
        ifc_close();
        return -1;
    }
    ifc_close();
    get_dhcp_info(ipaddr, gateway, mask, dns1, dns2, server, lease);
    return 0;
}

const char *get_dhcp_error_string() {
    return dhcp_lasterror();
}

static int check_driver_loaded() {
    char driver_status[PROPERTY_VALUE_MAX];
    FILE *proc;
    char line[sizeof(DRIVER_MODULE_TAG)+10];

    if (!property_get(DRIVER_PROP_NAME, driver_status, NULL)
            || strcmp(driver_status, "ok") != 0) {
        return 0;  /* driver not loaded */
    }
    /*
     * If the property says the driver is loaded, check to
     * make sure that the property setting isn't just left
     * over from a previous manual shutdown or a runtime
     * crash.
     */
    if ((proc = fopen(MODULE_FILE, "r")) == NULL) {
        LOGW("Could not open %s: %s", MODULE_FILE, strerror(errno));
        property_set(DRIVER_PROP_NAME, "unloaded");
        return 0;
    }
    while ((fgets(line, sizeof(line), proc)) != NULL) {
        if (strncmp(line, DRIVER_MODULE_TAG, strlen(DRIVER_MODULE_TAG)) == 0) {
            fclose(proc);
            return 1;
        }
    }
    fclose(proc);
    property_set(DRIVER_PROP_NAME, "unloaded");
    return 0;
}

int wifi_load_driver()
{
#ifdef RTL_USB_WIFI_USED

    char driver_status[PROPERTY_VALUE_MAX];
    int count = 100; /* wait at most 20 seconds for completion */
    char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
    
    unsigned char tmp_buf[200] = {0};
    char *p_strstr  = NULL;
    int  ret        = 0;
    FILE *fp        = NULL;
    int sleep_count = 0;
    
    /* Check whether is stopping */
    if (property_get(SUPP_PROP_NAME, supp_status, NULL)
            && strcmp(supp_status, "stopping") == 0) {
        LOGD("[wifiHW] wpa status is stopping!");    	
        LOGD("[wifiHW] try to stop supplicant again...");    	
        wifi_stop_supplicant();
        property_get(SUPP_PROP_NAME, supp_status, NULL);
        LOGD("[wifiHW] supp_status = %s", supp_status);    	
    }    
    
    LOGD("start to isnmod rtl8192cu.ko\n");
    
    if (insmod(DRIVER_MODULE_PATH, DRIVER_MODULE_ARG) < 0) {
        LOGE("insmod rtl8192cu ko failed!");
        #if defined NANO_SDIO_WIFI_USED 
        rmmod(DRIVER_MODULE_NAME);          
        #endif
        return -1;
    }    

    do{
       fp=fopen("/proc/net/wireless", "r");
       if (!fp) {
               LOGE("failed to fopen file /proc/net/wireless\n");
               return -1;
       }
       ret = fread(tmp_buf, 200, 1, fp);
       if (ret==0){
               LOGE("in hardware wifi_load_driver, faied to read proc/net/wireless");
       }
       fclose(fp);

       LOGE("in hardware wifi_load_driver, it is running to insmod wifi driver");
       p_strstr = strstr(tmp_buf, "wlan0");
       if (p_strstr != NULL) {
               break;
       }
       usleep(200000);

   } while (sleep_count++ <=10);

   if(sleep_count > 10) {
       LOGE("in hardware wifi_load_driver, timeout to poll wlan0");
       rmmod(DRIVER_MODULE_NAME); 
       return -1;
   }

   return 0;
    
#else 
	
    char driver_status[PROPERTY_VALUE_MAX];
    int count = 100; /* wait at most 20 seconds for completion */
    char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
    
    if (property_get(SUPP_PROP_NAME, supp_status, NULL)
            && strcmp(supp_status, "stopping") == 0) {
        LOGD("[wifiHW] wpa status is stopping!");    	
        LOGD("[wifiHW] try to stop supplicant again.......");    	
        wifi_stop_supplicant();
        property_get(SUPP_PROP_NAME, supp_status, NULL);
        LOGD("[wifiHW] supp_status = %s", supp_status);    	
    }   
    
    if (check_driver_loaded()) {
        return 0;
    }    
#ifdef NANO_SDIO_WIFI_USED    
    LOGD("begin insmod [nano] wifi firmware!");
    // load firmware, add by weiziheng 2011-06-21
    if(insmod(FIRMWARE_MODULE_PATH,FIRMWARE_MODULE_ARG) < 0) {
        LOGE("insmod [nano] wifi firmware failed!");
        rmmod(DRIVER_MODULE_NAME);
        rmmod(FIRMWARE_MODULE_NAME);
        return -1;
    }
#endif    
    if (insmod(DRIVER_MODULE_PATH, DRIVER_MODULE_ARG) < 0) {
        LOGE("insmod wifi ko failed!");
        #if defined NANO_SDIO_WIFI_USED 
        rmmod(FIRMWARE_MODULE_NAME);          
        #endif
        return -1;
    }
    
    if (strcmp(FIRMWARE_LOADER,"") == 0) {
#ifdef NANO_SDIO_WIFI_USED	    	
		unsigned char tmp_buf[200] = {0};  	
		FILE *profs_entry = NULL;
		int try_time = 0;	
		do {		
			profs_entry = fopen("/proc/net/wireless", "r");
			if(profs_entry == NULL){
				LOGE("[wifiHW] open /proc/net/wireless failed!");
				property_set(DRIVER_PROP_NAME, "failed");
				break;
		    }
		    
	        if( 0 == fread(tmp_buf, 200, 1, profs_entry) ){
	            LOGD("[wifiHW] faied to read proc/net/wireless");
	        }
			
			if(strstr(tmp_buf, "wlan0")) {
				LOGD("[wifiHW] insmod okay,try_time(%d)", try_time);
			    fclose(profs_entry);
			    profs_entry = NULL;
			    property_set(DRIVER_PROP_NAME, "ok");
			    break;			    
			}else {
				LOGD("[wifiHW] nano initial,try_time(%d)",try_time);
				property_set(DRIVER_PROP_NAME, "failed");				    		
			}			
	        fclose(profs_entry);
	        profs_entry = NULL;				
			usleep(200000);
		}while(try_time++ <= 20);// 4 seconds		
#else
        usleep(WIFI_DRIVER_LOADER_DELAY);
        property_set(DRIVER_PROP_NAME, "ok");
#endif		
    }
    else {
        property_set("ctl.start", FIRMWARE_LOADER);
    }
    sched_yield();
    while (count-- > 0) {
        if (property_get(DRIVER_PROP_NAME, driver_status, NULL)) {
            if (strcmp(driver_status, "ok") == 0)
                return 0;
            else if (strcmp(DRIVER_PROP_NAME, "failed") == 0) {
                wifi_unload_driver();
                return -1;
            }
        }
        usleep(200000);
    }
    property_set(DRIVER_PROP_NAME, "timeout");
    wifi_unload_driver();
    return -1;
#endif    
}

int wifi_unload_driver()
{
    int count = 20; /* wait at most 10 seconds for completion */

	LOGD("wifi unload driver.\n");

    if ( (rmmod(DRIVER_MODULE_NAME) == 0) 
#ifdef     NANO_SDIO_WIFI_USED
            && (rmmod(FIRMWARE_MODULE_NAME) == 0) 
#endif
            ) {
    	while (count-- > 0) {
    	    if (!check_driver_loaded())
    		break;
        	    usleep(500000);
    	}
    	if (count) {
        	return 0;
    	}
    	return -1;
    } else
        return -1;

}

int ensure_config_file_exists()
{
    char buf[2048];
    int srcfd, destfd;
    int nread;

    if (access(SUPP_CONFIG_FILE, R_OK|W_OK) == 0) {
        return 0;
    } else if (errno != ENOENT) {
        LOGE("Cannot access \"%s\": %s", SUPP_CONFIG_FILE, strerror(errno));
        return -1;
    }

    srcfd = open(SUPP_CONFIG_TEMPLATE, O_RDONLY);
    if (srcfd < 0) {
        LOGE("Cannot open \"%s\": %s", SUPP_CONFIG_TEMPLATE, strerror(errno));
        return -1;
    }

    destfd = open(SUPP_CONFIG_FILE, O_CREAT|O_WRONLY, 0660);
    if (destfd < 0) {
        close(srcfd);
        LOGE("Cannot create \"%s\": %s", SUPP_CONFIG_FILE, strerror(errno));
        return -1;
    }

    while ((nread = read(srcfd, buf, sizeof(buf))) != 0) {
        if (nread < 0) {
            LOGE("Error reading \"%s\": %s", SUPP_CONFIG_TEMPLATE, strerror(errno));
            close(srcfd);
            close(destfd);
            unlink(SUPP_CONFIG_FILE);
            return -1;
        }
        write(destfd, buf, nread);
    }

    close(destfd);
    close(srcfd);

    if (chown(SUPP_CONFIG_FILE, AID_SYSTEM, AID_WIFI) < 0) {
        LOGE("Error changing group ownership of %s to %d: %s",
             SUPP_CONFIG_FILE, AID_WIFI, strerror(errno));
        unlink(SUPP_CONFIG_FILE);
        return -1;
    }
    return 0;
}

int wifi_start_supplicant()
{
    char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
    int count = 200; /* wait at most 20 seconds for completion */
#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
    const prop_info *pi;
    unsigned serial = 0;
#endif

    /* Check whether already running */
    if (property_get(SUPP_PROP_NAME, supp_status, NULL)
            && strcmp(supp_status, "running") == 0) {
        LOGD("[wifiHW] wpa is running already");    	
        return 0;
    }

    /* Before starting the daemon, make sure its config file exists */
    if (ensure_config_file_exists() < 0) {
        LOGE("Wi-Fi will not be enabled");
        return -1;
    }

    /* Clear out any stale socket files that might be left over. */
    wpa_ctrl_cleanup();

#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
    /*
     * Get a reference to the status property, so we can distinguish
     * the case where it goes stopped => running => stopped (i.e.,
     * it start up, but fails right away) from the case in which
     * it starts in the stopped state and never manages to start
     * running at all.
     */
    pi = __system_property_find(SUPP_PROP_NAME);
    if (pi != NULL) {
        serial = pi->serial;
    }
#endif
    property_set("ctl.start", SUPPLICANT_NAME);
    sched_yield();

    while (count-- > 0) {
#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
        if (pi == NULL) {
            pi = __system_property_find(SUPP_PROP_NAME);
        }
        if (pi != NULL) {
            __system_property_read(pi, NULL, supp_status);
            if (strcmp(supp_status, "running") == 0) {
            	LOGD("[wifiHW] wpa running");
                return 0;
            } else if (pi->serial != serial &&
                    strcmp(supp_status, "stopped") == 0) {
                return -1;
            }
        }
#else
        if (property_get(SUPP_PROP_NAME, supp_status, NULL)) {
            if (strcmp(supp_status, "running") == 0){
            	LOGD("[wifiHW] wpa running");
                return 0;
            }
        }
#endif
        usleep(100000);
    }
    return -1;
}

int wifi_stop_supplicant()
{
    char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
    int count = 50; /* wait at most 5 seconds for completion */
	LOGD("Enter wifi stop supplicant");
    /* Check whether supplicant already stopped */
    if (property_get(SUPP_PROP_NAME, supp_status, NULL)
        && strcmp(supp_status, "stopped") == 0) {
        return 0;
    }

    property_set("ctl.stop", SUPPLICANT_NAME);
    sched_yield();

    while (count-- > 0) {
        if (property_get(SUPP_PROP_NAME, supp_status, NULL)) {
            if (strcmp(supp_status, "stopped") == 0)
                return 0;
        }
        usleep(100000);
    }
    LOGD("[wifiHW] stop supplicant FAILED");	
    return -1;
}

#define SUPPLICANT_TIMEOUT      3000000  // microseconds
#define SUPPLICANT_TIMEOUT_STEP  100000  // microseconds

int wifi_connect_to_supplicant()
{
    char ifname[256];
    char supp_status[PROPERTY_VALUE_MAX] = {'\0'};
    int  supplicant_timeout = SUPPLICANT_TIMEOUT;

	LOGD("[wifiHW] wifi connect to supplicant");
    /* Make sure supplicant is running */
    if (!property_get(SUPP_PROP_NAME, supp_status, NULL)
            || strcmp(supp_status, "running") != 0) {
        LOGE("Supplicant not running, cannot connect");
        return -1;
    }

    property_get("wifi.interface", iface, WIFI_TEST_INTERFACE);

    if (access(IFACE_DIR, F_OK) == 0) {
        snprintf(ifname, sizeof(ifname), "%s/%s", IFACE_DIR, iface);
    } else {
        strlcpy(ifname, iface, sizeof(ifname));
    }

    ctrl_conn = wpa_ctrl_open(ifname);
    while (ctrl_conn == NULL && supplicant_timeout > 0) {
        usleep(SUPPLICANT_TIMEOUT_STEP);
        supplicant_timeout -= SUPPLICANT_TIMEOUT_STEP;
        ctrl_conn = wpa_ctrl_open(ifname);
    }
    if (ctrl_conn == NULL) {
        LOGE("Unable to open connection to supplicant on \"%s\": %s",
             ifname, strerror(errno));
        return -1;
    }
        
    monitor_conn = wpa_ctrl_open(ifname);
    if (monitor_conn == NULL) {
        wpa_ctrl_close(ctrl_conn);
        ctrl_conn = NULL;
        return -1;
    }
    if (wpa_ctrl_attach(monitor_conn) != 0) {
        wpa_ctrl_close(monitor_conn);
        wpa_ctrl_close(ctrl_conn);
        ctrl_conn = monitor_conn = NULL;
        return -1;
    }
    return 0;
}

int wifi_send_command(struct wpa_ctrl *ctrl, const char *cmd, char *reply, size_t *reply_len)
{
    int ret;

    if (ctrl_conn == NULL) {
        LOGV("Not connected to wpa_supplicant - \"%s\" command dropped.\n", cmd);
        return -1;
    }
    ret = wpa_ctrl_request(ctrl, cmd, strlen(cmd), reply, reply_len, NULL);
    if (ret == -2) {
        LOGD("'%s' command timed out.\n", cmd);
        return -2;
    } else if (ret < 0 || strncmp(reply, "FAIL", 4) == 0) {
        return -1;
    }
    if (strncmp(cmd, "PING", 4) == 0) {
        reply[*reply_len] = '\0';
    }
    return 0;
}

int wifi_wait_for_event(char *buf, size_t buflen)
{
    size_t nread = buflen - 1;
    int fd;
    fd_set rfds;
    int result;
    struct timeval tval;
    struct timeval *tptr;
    
    if (monitor_conn == NULL) {
        LOGD("Connection closed\n");
        strncpy(buf, WPA_EVENT_TERMINATING " - connection closed", buflen-1);
        buf[buflen-1] = '\0';
        return strlen(buf);
    }

    result = wpa_ctrl_recv(monitor_conn, buf, &nread);
    if (result < 0) {
        LOGD("wpa_ctrl_recv failed: %s\n", strerror(errno));
        strncpy(buf, WPA_EVENT_TERMINATING " - recv error", buflen-1);
        buf[buflen-1] = '\0';
        return strlen(buf);
    }
    buf[nread] = '\0';
    /* LOGD("wait_for_event: result=%d nread=%d string=\"%s\"\n", result, nread, buf); */
    /* Check for EOF on the socket */
    if (result == 0 && nread == 0) {
        /* Fabricate an event to pass up */
        LOGD("Received EOF on supplicant socket\n");
        strncpy(buf, WPA_EVENT_TERMINATING " - signal 0 received", buflen-1);
        buf[buflen-1] = '\0';
        return strlen(buf);
    }
    /*
     * Events strings are in the format
     *
     *     <N>CTRL-EVENT-XXX 
     *
     * where N is the message level in numerical form (0=VERBOSE, 1=DEBUG,
     * etc.) and XXX is the event name. The level information is not useful
     * to us, so strip it off.
     */
    if (buf[0] == '<') {
        char *match = strchr(buf, '>');
        if (match != NULL) {
            nread -= (match+1-buf);
            memmove(buf, match+1, nread+1);
        }
    }
    return nread;
}

void wifi_close_supplicant_connection()
{
    if (ctrl_conn != NULL) {
        wpa_ctrl_close(ctrl_conn);
        ctrl_conn = NULL;
    }
    if (monitor_conn != NULL) {
        wpa_ctrl_close(monitor_conn);
        monitor_conn = NULL;
    }
}

int wifi_command(const char *command, char *reply, size_t *reply_len)
{
    return wifi_send_command(ctrl_conn, command, reply, reply_len);
}
