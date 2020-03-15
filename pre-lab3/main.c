#include <time.h>
#include <stdlib.h>

#include "rte_common.h"
#include "rte_mbuf.h"

#include "qos.h"

int
main(int argc, char **argv)
{
    int ret, i, j;

    /** init EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    qos_meter_init();
    qos_dropper_init();

    srand(time(NULL));
    uint64_t time = 0;
    int cnt_send[APP_FLOWS_MAX];
    int cnt_pass[APP_FLOWS_MAX];
    int cnt_color[APP_FLOWS_MAX][3];
    for (i = 0; i < APP_FLOWS_MAX; i++) {
        cnt_send[i] = cnt_pass[i] = 0;
        for (j = 0; j < 3; j++) 
            cnt_color[i][j] = 0;
    }

    for (i = 0; i < 10; i++) {
        /** 1000 packets per period averagely */
        int burst = 500 + rand() % 1000;

        for (j = 0; j < burst; j++) {
            uint32_t flow_id = (uint32_t)(rand() % APP_FLOWS_MAX);
            
            /** 640 bytes per packet averagely */
            uint32_t pkt_len = (uint32_t)(128 + rand() % 1024);
        
            /** get color */
            enum qos_color color = qos_meter_run(flow_id, pkt_len, time);

            
            /** make decision: weather drop */
            int pass = qos_dropper_run(flow_id, color, time);

            cnt_send[flow_id] += pkt_len;
            cnt_pass[flow_id] += pass ? 0 : pkt_len;
            cnt_color[flow_id][color] += 1;
        }
        time += 1000000;
    }

    for (i = 0; i < APP_FLOWS_MAX; i++) {
        printf("fid: %d, send: %d, pass: %d, green: %d, yellow: %d, red: %d\n", i,
                cnt_send[i], cnt_pass[i], cnt_color[i][0], cnt_color[i][1],
                cnt_color[i][2]);
    }

    return 0;
}