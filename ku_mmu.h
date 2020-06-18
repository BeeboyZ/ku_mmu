#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define PDI_SHIFT 6
#define PDMI_SHIFT 4
#define PTI_SHIFT 2
#define PDI 192
#define PMDI 48
#define PTI 12
#define PO 3

struct ku_pte
{
    char pte;
};

struct qNODE {
    struct qNODE *next;
    char pid;
    char va;
};

struct lNODE {
    struct lNODE *next;
    char pid;
    struct ku_pte* pdbr;
};

struct Queue {
    struct qNODE* front; 
    struct qNODE* rear;
    int count;
};

unsigned int ku_mmu_men_size;
unsigned int ku_mmu_swap_size;
struct Queue ku_mmu_swap_queue;
struct lNODE* ku_mmu_list;
struct ku_pte* ku_mmu_pmem; //물리 메모리
int* ku_mmu_freelist;
int* ku_mmu_swapspace;

//스왑큐를 초기화 시켜준다
void initQueue(struct Queue* queue){
    queue->front = queue->rear=NULL;
    queue->count=0;
}

//스왑큐가 비어있을때 실행
int isEmpty(struct Queue* queue){
    return queue->count==0;
}

//페이지 할당할때 큐에 삽입
void enqueue(char pid, char va, struct Queue* queue){
    struct qNODE* now = (struct qNODE*)malloc(sizeof(struct qNODE));
    now->pid=pid;
    now->va=va;
    now->next=NULL;

    if(isEmpty(queue)){
        queue->front=now;
    }
    else{
        queue->rear->next=now;
    }
    queue->rear=now;
    queue->count++;
}

//메모리가 꽉차면 페이지 스왑아웃
int dequeue(char* pid, char* va, struct Queue* queue){
    struct qNODE* now;
    if (isEmpty(queue)){
        return -1;
    }
    now = queue->front;
    *pid=now->pid;
    *va=now->va;
    queue->front=now->next;
    free(now);
    queue->count--;
    return 0;
}


//모든 전역변수 초기화
void *ku_mmu_init (unsigned int mem_size, unsigned int swap_size){
    ku_mmu_men_size=mem_size;
    ku_mmu_swap_size=swap_size;
    initQueue(&ku_mmu_swap_queue);
    ku_mmu_list=NULL;
    ku_mmu_pmem=calloc(mem_size,sizeof(struct ku_pte));
    ku_mmu_freelist=calloc(mem_size,sizeof(int));
    ku_mmu_swapspace=calloc(swap_size,sizeof(int));
    if(ku_mmu_pmem==NULL || ku_mmu_freelist==NULL || ku_mmu_swapspace==NULL){
        return 0;
    }
    else{
        return ku_mmu_pmem;
    }
}

//pid를 가지고 해당 프로세스의 pdbr을 탐색후 반환
struct ku_pte* returnPDBR(char pid){
    struct lNODE* curr = ku_mmu_list;
    if (ku_mmu_list==NULL){
        return 0;
    }
    else{
        while(curr!=NULL){
            if(curr->pid==pid){
            return curr->pdbr;
            }
        curr=curr->next;
        }
        return NULL;
    }
}

//비어있는 페이지가 있으면 할당하고 없으면 스왑아웃후 할당
int page_alloc(){
    int i;
    for(i=4;i<ku_mmu_men_size;i+=4){
        if(ku_mmu_freelist[i]==0){
            ku_mmu_freelist[i]=1;
            return i;
        }
    }
        char pid,va;
        if(-1==dequeue(&pid,&va,&ku_mmu_swap_queue)){
            return -1;
        }
        else{
            char pdi=(va & PDI) >> PDI_SHIFT;
            char pmdi=(va & PMDI) >> PDMI_SHIFT;
            char pti=(va & PTI) >> PTI_SHIFT;
            char po=(va & PO);
            struct ku_pte* pdbr=returnPDBR(pid);
            char pmdi_pfn=(pdbr+pdi)->pte >> 2;
            char pti_pfn=ku_mmu_pmem[pmdi_pfn*4+pmdi].pte >> 2;
            char page=ku_mmu_pmem[pti_pfn*4+pti].pte >> 2;
            
            for(int i=4;i<ku_mmu_swap_size;i+=4){
                if(ku_mmu_swapspace[i]==0){
                    ku_mmu_swapspace[i]=1;//스왑아웃
                    ku_mmu_pmem[pti_pfn*4+pti].pte=i<<1;//스왑스페이스에 넣는다
                    ku_mmu_freelist[page*4]=1;
                    return page*4;//공간의 인덱스값 반환
                }
            }
            return -1;
        }
    }
    

//pid를 받아서 프로세스를 새로 실행하거나 context switch를 한다
int ku_run_proc(char pid, struct ku_pte** ku_cr3){
    struct ku_pte* pdbr=returnPDBR(pid);
    if(pdbr==NULL){
        int index=page_alloc();
        if(index== -1){
            return -1;
        }
        else{
            struct lNODE* new = (struct lNODE*)malloc(sizeof(struct lNODE));
            new->pid=pid;
            new->pdbr=&ku_mmu_pmem[index];
            new->next=NULL;
            new->next=ku_mmu_list;
            ku_mmu_list=new;
            *ku_cr3=&ku_mmu_pmem[index];
            return 0;
        }
    }
    else{
        *ku_cr3=pdbr;
        return 0;
    }
};

int ku_page_fault(char pid, char va){
    struct ku_pte* pdbr=returnPDBR(pid);
    //page directory에서 pagemiddlediretory에 접근
    char pdi=(va & PDI) >> PDI_SHIFT;
    char pmdi_pte=(pdbr+pdi)->pte;
    char pmdi_pfn;
    if(pmdi_pte==0B00000000){
        int index=page_alloc();
        (pdbr+pdi)->pte = index | 0x1;//(index/4)<<2 | 0x1와 결과값 동일
        pmdi_pfn=(pdbr+pdi)->pte >> 2;;
    }
    else{
        pmdi_pfn=pmdi_pte >> 2;;
    }
    //pagemiddledirectory에서 pagetable접근
    char pmdi=(va & PMDI) >> PDMI_SHIFT;
    char pti_pte=ku_mmu_pmem[pmdi_pfn*4+pmdi].pte;
    char pti_pfn;
    if(pti_pte==0B00000000){
        int index=page_alloc();
        ku_mmu_pmem[pmdi_pfn*4+pmdi].pte = index | 0x1;
        pti_pfn=ku_mmu_pmem[pmdi_pfn*4+pmdi].pte >> 2;
    }
    else{
        pti_pfn=pti_pte >> 2;
    }
    //pagetable에서 page접근
    char pti=(va & PTI) >> PTI_SHIFT;
    char po=(va & PO);
    char page=ku_mmu_pmem[pti_pfn*4+pti].pte >> 2;
    if(page==0B00000000){
        int index=page_alloc();
        ku_mmu_pmem[pti_pfn*4+pti].pte = index | 0x1;
        enqueue(pid,va,&ku_mmu_swap_queue);
        return 0;
    }
    else {
        if(page << 7 == 0){
            ku_mmu_swapspace[ku_mmu_pmem[pti_pfn*4+pti].pte>>1]=0;
            int index=page_alloc();
            ku_mmu_pmem[pti_pfn*4+pti].pte = index | 0x1;
            enqueue(pid,va,&ku_mmu_swap_queue);
            return 0;
        }
        else if(page << 7 == 128){
            return -1;
        }
    }
}