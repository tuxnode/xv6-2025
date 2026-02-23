#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

#define MAX_UDP_PKTS 16     // 每个端口最多缓存 16 个包
#define MAX_UDP_SOCKETS 10  // 系统同时绑定的最大端口数

struct sock {
  uint16 port;
  struct spinlock lock;
  char *pkts[MAX_UDP_PKTS]; // 存储指针的数组
  int head;                 // 存储头指针
  int tail;                 // 存储尾指针
};


// 全局socket表
struct {
  struct spinlock lock;
  struct sock socket[MAX_UDP_SOCKETS];
} socktable;


// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

extern int e1000_transmit(char *buf, int len);

void
netinit(void)
{
  initlock(&netlock, "netlock");

  // 初始化socket表
  initlock(&socktable.lock, "socktable lock");
  for(int i = 0; i < MAX_UDP_SOCKETS; ++i){
    initlock(&socktable.socket[i].lock, "sock lock");
    socktable.socket[i].port = 0;
    socktable.socket[i].head = 0;
    socktable.socket[i].tail = 0;
  }
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  //
  // Your code here.
  //

  int port;
  argint(0, &port);

  acquire(&socktable.lock);
  for(int i = 0;i < MAX_UDP_SOCKETS; ++i){
    if(socktable.socket[i].port == 0){
      socktable.socket[i].port = port;
      release(&socktable.lock);
      return 0;
    }
  }

  release(&socktable.lock);
  return -1;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //
  int port;
  argint(0, &port);

  acquire(&socktable.lock);
  for(int i = 0; i < MAX_UDP_SOCKETS; ++i){
    if(socktable.socket[i].port == port){
      acquire(&socktable.socket[i].lock);
      socktable.socket[i].port = 0;

      // 清空数组里还没读的包
      while(socktable.socket[i].head != socktable.socket[i].tail){
        kfree(socktable.socket[i].pkts[socktable.socket[i].head]);
        socktable.socket[i].head = (socktable.socket[i].head + 1) % MAX_UDP_PKTS;
        socktable.socket[i].head = 0;
        socktable.socket[i].tail = 0;
      }
      release(&socktable.socket[i].lock);
      break;
    } 
  }

  release(&socktable.lock);
  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  // Your code here.

int dport, maxlen;
  uint64 src_addr, sport_addr, buf_addr;

  // 获取用户传入的 5 个参数
  argint(0, &dport); argaddr(1, &src_addr);
  argaddr(2, &sport_addr); argaddr(3, &buf_addr);
  argint(4, &maxlen);

  // 在全局表中找到匹配的 socket
  struct sock *s = 0;
  acquire(&socktable.lock);
  for(int i = 0; i < MAX_UDP_SOCKETS; i++){
    if(socktable.socket[i].port == dport){
      s = &socktable.socket[i];
      break;
    }
  }
  release(&socktable.lock);

  if(!s) return -1;

  // 等待数据包
  acquire(&s->lock);
  while(s->head == s->tail){
    sleep(s, &s->lock);
  }

  // 从数组head取出一个包
  char *pkt = s->pkts[s->head];
  s->head = (s->head + 1) % MAX_UDP_PKTS;
  release(&s->lock);

  // 4. 解析网络包，提取所需字段
  struct eth *eth = (struct eth *)pkt;
  struct ip *ip = (struct ip *)(eth + 1); // 跳过以eth header
  int ip_head_len = (ip->ip_vhl & 0x0f) * 4;
  struct udp *udp = (struct udp *)((char *)ip + ip_head_len); // 跳过ip header

  uint32 src_ip = ntohl(ip->ip_src);    // 转为主机字节序
  uint16 sport = ntohs(udp->sport);     // 转为主机字节序
  int payload_len = ntohs(udp->ulen) - sizeof(struct udp);
  char *payload = (char *)(udp + 1);

  // 拷贝回用户空间
  struct proc *p = myproc();
  if(copyout(p->pagetable, src_addr, (char *)&src_ip, sizeof(src_ip)) < 0) goto err;
  if(copyout(p->pagetable, sport_addr, (char *)&sport, sizeof(sport)) < 0) goto err;
  
  int copylen = (payload_len < maxlen) ? payload_len : maxlen;
  if(copyout(p->pagetable, buf_addr, payload, copylen) < 0) goto err;

  // 清理内存
  kfree(pkt);
  return copylen;

err:
  kfree(pkt);
  return -1;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;
  // Your code here.
  struct eth *eth = (struct eth *) buf;
  struct ip *inip = (struct ip *) (eth + 1);

  // 判断是否是正确的ipv4 package
  if((inip->ip_vhl >> 4) != 4){
    panic("ip_rx: ip_version fault");
    kfree((void *) buf);
    return;
  }

  // 判断上游协议
  if(inip->ip_p == IPPROTO_TCP){
    kfree((void *) buf);
    return;
  }

  if(inip->ip_p == IPPROTO_UDP){
    int ip_head_len = (inip->ip_vhl & 0x0f) * 4;
    struct udp *udp_header = (struct udp *) ((char *)inip + ip_head_len);
    uint16 dport = ntohs(udp_header->dport);

    // 检查是否有已经绑定的端口
    for(int i = 0; i < MAX_UDP_SOCKETS; ++i){
      struct sock *s = &socktable.socket[i];

      acquire(&s->lock);
      // 匹配到端口
      if(s->port == dport){
        // 检查数组是否已满
        int next_tail = (s->tail + 1) % MAX_UDP_PKTS;
        if(next_tail != s->head){
          // 将指针传入数组
          s->pkts[s->tail] = buf;
          s->tail = next_tail;

          // 唤醒进程
          wakeup(s);
          release(&s->lock);
          return;
        }
        // 数组满了要释放
        release(&s->lock);
        break;
      }
      release(&s->lock);
    }
  }
  kfree((void *) buf);
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
