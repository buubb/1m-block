#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <linux/types.h>
#include <linux/netfilter.h>		/* for NF_ACCEPT */
#include <errno.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <stdbool.h>
#include <libnetfilter_queue/libnetfilter_queue.h>

#define MAX_DATA 2
#define MAX_LINE 762564

char *target_site = NULL;

struct data{
    int number;
    char url[256];
};
struct data url_info[MAX_LINE];

// 비교 함수
int compare(const void *a, const void *b) {
    const struct data *dataA = (const struct data *)a;
    const struct data *dataB = (const struct data *)b;
    return strcmp(dataA->url, dataB->url);
}

// 이진 탐색 함수
int binarySearch(struct data *url_info, int left, int right, const char *url) {
    // 배열을 알파벳 순으로 정렬
    qsort(url_info, MAX_LINE, sizeof(struct data), compare);
    while (left <= right) {
        int mid = left + (right - left) / 2;
        int cmp = strncmp(url_info[mid].url, url, sizeof(url));
        if (cmp == 0)
            return 1; // 일치하는 URL 발견
        else if (cmp < 0)
            left = mid + 1;
        else
            right = mid - 1;
    }
    return 0; // 일치하는 URL 발견하지 못함
}

// 파일에서 URL을 읽어들여 사이트 배열에 저장하는 함수
int SiteListToArr(char *filename) {
    FILE* stream = fopen(filename, "r");
    char line[256];
    if(stream == NULL){
    	printf("File open error..\n");
    	fclose(stream);
    	return 0;
    }
    //fgets(line, 256, stream);
    
    char* ptr = strtok(line, ",");
    
    while(ptr != NULL){
    	printf("%s ", ptr);
    	ptr = strtok(NULL,",");
    }
    
    for (int i=0;i<MAX_LINE;i++){
    	fgets(line, 256, stream);
    	ptr = strtok(line, ",");
    	url_info[i].number = atoi(ptr);
    	
    	ptr = strtok(NULL, ",");
    	strcpy(url_info[i].url, ptr);
    }
    
    fclose(stream);
}
/* returns packet id */
static uint32_t print_pkt (struct nfq_data *tb, struct nfq_q_handle *qh)
{
	int id = 0;
	struct nfqnl_msg_packet_hdr *ph;
	struct nfqnl_msg_packet_hw *hwph;
	uint32_t mark, ifi, uid, gid;
	int ret;
	unsigned char *data, *secdata;
	ph = nfq_get_msg_packet_hdr(tb);
	if (ph) {
     	 id = ntohl(ph->packet_id);
  	 }

  	 hwph = nfq_get_packet_hw(tb);
  	 if (hwph) {
	      int i, hlen = ntohs(hwph->hw_addrlen);
	   }
	      
	ret = nfq_get_payload(tb, &data);
	struct iphdr *ip_info = (struct iphdr*)data;
    	struct tcphdr *tcp_info = (struct tcphdr*)(data+(ip_info->ihl*4));
    	uint8_t *payload = (uint8_t *)(data+(ip_info->ihl*4)+(tcp_info->doff*4));
    	

	if (ret >= 0){     
	      if (ip_info->protocol == IPPROTO_TCP) {
		      unsigned short d_port = ntohs(tcp_info->dest);
		        if (d_port == 0x0050 && payload[0] == 0x47) {
		             // get Host field
		             size_t x = 0;
		             for (size_t j = 22;; j++) {
		                 if (payload[j] == 0xD){
		                    x = j;
		                    break;
		                    }
		             }
		             char site[x-21]; // NULL 종료 문자까지 고려하여 배열 크기를 조정
			     for (size_t j = 22; j<x ; j++) {
    					site[j-22] = payload[j];   
			     }
			     site[x-22] = '\0'; // NULL 종료 문자 추가
			     int result = binarySearch(url_info, 0, MAX_LINE - 1, site);
			     if(result == 1){
			     	id = -1;
			     	printf("Blocked %s...\n", site);
			     } else {
			     	printf("Connected %s!!!\n", site);
			     }
				
			}   
		}
	}
	return id;
}

/* callback function*/
int cb(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg,
              struct nfq_data *nfa, void *data)
{
    
    uint32_t id = print_pkt(nfa, qh);
    if (id == -1){
    	return nfq_set_verdict(qh, id, NF_DROP, 0, NULL);
    } else{
    	return nfq_set_verdict(qh, id, NF_ACCEPT, 0, NULL);
	}
}



int main(int argc, char **argv)
{
	struct nfq_handle *h;
	struct nfq_q_handle *qh;
	int fd;
	int rv;
	uint32_t queue = 0;
	char buf[4096] __attribute__ ((aligned));

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <site list file>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	/* target site */
   	SiteListToArr(argv[1]);

	printf("opening library handle\n");
	h = nfq_open();
	if (!h) {
		fprintf(stderr, "error during nfq_open()\n");
		exit(1);
	}

	printf("unbinding existing nf_queue handler for AF_INET (if any)\n");
	if (nfq_unbind_pf(h, AF_INET) < 0) {
		fprintf(stderr, "error during nfq_unbind_pf()\n");
		exit(1);
	}

	printf("binding nfnetlink_queue as nf_queue handler for AF_INET\n");
	if (nfq_bind_pf(h, AF_INET) < 0) {
		fprintf(stderr, "error during nfq_bind_pf()\n");
		exit(1);
	}

	printf("binding this socket to queue '%d'\n", queue);
	
	// intercept packet
	qh = nfq_create_queue(h, queue, &cb, NULL);
	if (!qh) {
		fprintf(stderr, "error during nfq_create_queue()\n");
		exit(1);
	}

	printf("setting copy_packet mode\n");
	if (nfq_set_mode(qh, NFQNL_COPY_PACKET, 0xffff) < 0) {
		fprintf(stderr, "can't set packet_copy mode\n");
		exit(1);
	}

	printf("setting flags to request UID and GID\n");
	if (nfq_set_queue_flags(qh, NFQA_CFG_F_UID_GID, NFQA_CFG_F_UID_GID)) {
		fprintf(stderr, "This kernel version does not allow to "
				"retrieve process UID/GID.\n");
	}

	printf("setting flags to request security context\n");
	if (nfq_set_queue_flags(qh, NFQA_CFG_F_SECCTX, NFQA_CFG_F_SECCTX)) {
		fprintf(stderr, "This kernel version does not allow to "
				"retrieve security context.\n");
	}

	printf("Waiting for packets...\n");

	fd = nfq_fd(h);

	for (;;) {
		if ((rv = recv(fd, buf, sizeof(buf), 0)) >= 0) {
			// packet handling
			nfq_handle_packet(h, buf, rv);
			continue;
		}
		/* if your application is too slow to digest the packets that
		 * are sent from kernel-space, the socket buffer that we use
		 * to enqueue packets may fill up returning ENOBUFS. Depending
		 * on your application, this error may be ignored. Please, see
		 * the doxygen documentation of this library on how to improve
		 * this situation.
		 */
		if (rv < 0 && errno == ENOBUFS) {
			printf("losing packets!\n");
			continue;
		}
		perror("recv failed");
		break;
	}

	printf("unbinding from queue 0\n");
	nfq_destroy_queue(qh);

#ifdef INSANE
	/* normally, applications SHOULD NOT issue this command, since
	 * it detaches other programs/sockets from AF_INET, too ! */
	printf("unbinding from AF_INET\n");
	nfq_unbind_pf(h, AF_INET);
#endif

	printf("closing library handle\n");
	nfq_close(h);

	exit(0);
}
