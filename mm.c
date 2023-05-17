/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* 워드사이즈를 4로, 더블워드 사이즈를 8로 지정*/
#define WSIZE 4
#define DSIZE 8
/* 힙 크기 할당, 2**12 */
#define CHUNKSIZE (1<<12)


#define MAX(x,y) ((x)>(y) ? (x) :(y))                                           // 크기비교 후 큰 값 리턴
#define PACK(size,alloc) ((size) | (alloc))                                     // size는 8의 배수로 제공된다(아래에서) 따라서 위의 or 비트연산을 통해 1001 식으로 저장된다
#define GET(p) (*(unsigned int *)(p))                                           // 포인터가 가르키는 주소 리턴
#define PUT(p,val) (*(unsigned int *)(p)=(val))                                 // 포인터가 가르키는 주소에 value 저장
#define GET_SIZE(p) (GET(p) & ~0x7)                                             // p가 가르키는 주소에 있는 헤더 또는 푸터의 사이즈 리턴 (앤드 연산자를 통해 1001이 1000으로 리턴된다.)
#define GET_ALLOC(p) (GET(p) & 0x1)                                             // p가 가르키는 주소에 있는 헤더 또는 푸터의 할당 비트를 리턴

#define HDRP(bp) ((char *)(bp) - WSIZE)                                         //블록 헤더를 가르키는 주소
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)                    //블록 푸터를 가르키는 주소
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))         //다음 블록의 포인터 리턴 (현 블록의 bp에 1블록 크기(WSIZE)만큼 더함)
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))         //이전 블록의 포인터 리턴 (현 블록의 bp에 이전 블록 푸터에서 참조한 크기 만큼 리턴)


/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)                           //더블 워드 기준으로 맞추기 위해 칸을 추가 0~7까지 추가생성함


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))                                     //size_t: 해당 시스템에서 객체나 값이 포함할 수 있는 최대 크기의 데이터
static char *heap_listp;                                                        //항상 prologe block을 가리키는 정적 전역 변수 설정

#define NEXT_FIT  // define하면 next_fit, 안 하면 first_fit으로 탐색

//미리 선언
int mm_init(void);                                                              
void *mm_malloc(size_t);
void mm_free(void *);
void *mm_realloc(void *, size_t);
static void *extend_heap(size_t);
static void *coalesce(void *);
static void *find_fit(size_t);
static void place(void *, size_t);
#ifdef NEXT_FIT
    static void* last_freep;  // next_fit 사용 시 마지막으로 탐색한 가용 블록
#endif

// 함수 선언 시작
/**
 * pg821 그림 9.40에 있는 내용, 앞 뒤 블록의 가용 여부를 확인하여 블록을 연결할지 아닐지를 결정한다.
 * @param bp 블록 포인터
 * 부분적으로 bp 리턴됨
*/
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));                             // 이전 블록의 푸터의 alloc 값을 확인하여 현재 사용 여부를 확인 
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));                             // 다음 블록의 헤더의 alloc 값을 확인하여 현재 사용 여부를 확인
    size_t size = GET_SIZE(HDRP(bp));                                               // size는 현재 헤더에 있는 블록의 크기를 확인

    if (prev_alloc && next_alloc) {                                                 // 양쪽 블록 모두 현재 사용중인 블록일 때
        return bp;                                                                  // 현재의 포인터 반환
    }

    else if (prev_alloc && !next_alloc) {                                           // 이전 블록만 사용 중 (다음 블록이 비어 있어 연결 가능)
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));                                      // 사이즈 조정 (현재 블록 사이즈 + 다음 블록 사이즈)
        PUT(HDRP(bp),PACK(size,0));                                                 // 헤더 포인터 위치는 그대로 유지, 사이즈만 변경
        PUT(FTRP(bp),PACK(size,0));                                                 // 푸터 포인터 위치를 이후 블록 푸터의 위치로 변경, 사이즈 변경  [  FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)  ]
    }

    else if (!prev_alloc && next_alloc) {                                           // 다음 블록만 사용 중 (이전 블록 비어 있어 연결 가능)
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));                                      // 사이즈 조정 (현재 블록 사이즈 + 이전 블록 사이즈)
        PUT(FTRP(bp),PACK(size,0));                                                 // 푸터 포인터 위치는 그대로 유지, 사이즈만 변경
        PUT(HDRP(PREV_BLKP(bp)),PACK(size,0));                                      // 헤더 포인터 위치는 이전 블록 헤더로, 사이즈 변경
        bp= PREV_BLKP(bp);                                                          // bp는 이전 블록의 헤더 뒤로 위치 변경
    }

    else {                                                                              // 양쪽 블록 모두 사용하고있지 않아 연결 가능할 때
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));      // 사이즈 조정 (현재 블록 사이즈 + 이전 블록 사이즈 + 다음 블록 사이즈)
        PUT(HDRP(PREV_BLKP(bp)),PACK(size,0));                                      // 헤더 포인터 위치 이전 블록 헤더로, 사이즈 조정
        PUT(FTRP(NEXT_BLKP(bp)),PACK(size,0));                                      // 푸터 포인터 위치 이후 블록 푸터로, 사이즈 조정
        bp = PREV_BLKP(bp);                                                         // 블록 포인터는 이전 블록 포인터 위치로
    }   

    #ifdef NEXT_FIT
        last_freep = bp;                                                            // NEXT-FIT 을 사용하는 경우 현재 합친 bp를 last freep로 저장한다.
    #endif

    return bp;                                                                      // bp 반환
}

/**
 * 워드 단위 메모리를 인자로 받아 힙의 크기를 증가시킴
 * @param words 
 * 
*/

static void *extend_heap(size_t words)                                              //워드 사이즈로 받는다 (입력할 때 보면 CHUNKSIZE/WSIZE로 입력받는다.)
{
    char *bp;
    size_t size;
    
    size = (words % 2) ? (words +1) * WSIZE : words * WSIZE;                        //짝수인지(doubleword인지) 를 확인하고 아닌 경우 더블워드가 되도록 확장하는 과정
    if ((long)(bp = mem_sbrk(size)) == -1)                                          //새 메모리의 첫 부분을 bp로 놓는다.(mem_sbrk 함수를 보면 확장하기 전 힙의 마지막 위치를 리턴한다.)
        return  NULL;
    PUT(HDRP(bp), PACK(size,0));                                                    //포인터가 가리키는 헤더에 값 사이즈 입력, alloc은 0이므로 빈 메모리임을 보여줌
    PUT(FTRP(bp), PACK(size,0));                                                    //포인터가 가리키는 푸터에 값 사이즈 입력, alloc은 0이므로 빈 메모리임을 보여줌
    PUT(HDRP(NEXT_BLKP(bp)),PACK(0,1));                                             //새 에필로그 헤더

    return coalesce(bp);                                                            //이전 블록을 사용하고있지 않다면 연결함
}

/**
 * initialize the malloc package.
 * 말록함수 초기화
 * 
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);                                                             //더블워드 경계로 정렬된 미사용 패딩
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE,1));                                     //프롤로그 헤더
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE,1));                                     //프롤로그 푸터
    PUT(heap_listp + (3*WSIZE), PACK(0,1));                                         //에필로그 헤더
    heap_listp += (2*WSIZE);                                                        //정적 전역 변수는 프롤로그 블록을 가리키도록 위의 프롤로그 헤더와 p 포인터 위치를 맞춘다.

    #ifdef NEXT_FIT
        last_freep = heap_listp;
    #endif

    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)                                       //청크사이즈만큼 확장
        return -1;                                                                  //오류시 -1 리턴

    return 0;                                                                       //오류가 아니라면 0 리턴해서 운영체제에 잘 작동하고 있음을 반환
}

/**
 * 동적 할당할 메모리 크기를 받아 알맞은 크기의 블록을 할당. 후보 없으면 힙을 청크사이즈만큼 늘려 할당
 * 할당받은 블록의 포인터 반환
 */
void *mm_malloc(size_t size)
{
    //블록사이즈 조정 size를 조절할 수 없고, 어짜피 DSIZE 크기 단위로 움직여야 하므로 size를 DSIZE기준으로 확장시킨 asize를 활용할 예정
    size_t asize;                                                                   
    size_t extendsize;                                                              //확장할 힙의 양 조정
    char *bp;
    //가짜 요청 무시
    if (size ==0)
        return NULL;
    if (size<=DSIZE)                                                                //사이즈가 DoubleWord사이즈보다 작다면
        asize = 2*DSIZE;                                                            //asize = 16 (Doubleword 사이즈의 2배)로 조절
    else                                                                            //사이즈가 Doubleword 사이즈보다 크다면
        asize = DSIZE * ((size + (DSIZE) + (DSIZE -1))/ DSIZE);                     //size가 DSIZE*2의 배수가 되도록 확장시켜 asize에 저장
    if ((bp= find_fit(asize)) !=NULL) {                                             //주소를 찾아 할당할 수 있는 블록이 있다면 할당한다.
        place(bp,asize);                                                            //asize에 맞게 블록의 크기를 축소하여 할당할 수 있다면 축소시킨다.
        return bp;                                                                  //현재 bp를 리턴하고 말록함수를 멈춘다.
    }
                                                                                    //만약 할당할 수 있는 블럭이 없다면
    extendsize=MAX(asize,CHUNKSIZE);                                                //청크사이즈와 asize를 비교해 더 큰 값 만큼
    if ((bp=extend_heap(extendsize/WSIZE)) == NULL)                                 //확장한다.
        return NULL;                                                                //오류시 NULL 리턴
    place(bp,asize);                                                                //축소할 수 있다면 축소시킨다.
    return bp;                                                                      //확장된 bp를 리턴
}

/**
 * 해당 주소의 블록 반환.
 * 
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));                                               //헤더를 통해 사이즈 확인(푸터 위치를 확인하기 위해)
    PUT(HDRP(bp),PACK(size,0));                                                     //헤더의 정보에 사이즈 설정, 
    PUT(FTRP(bp),PACK(size,0));                                                     //푸터의 정보에 사이즈 설정
    coalesce(bp);                                                                   //전후 블록 확인해서 가용상태면 연결
}

/**
 * 입력받은 사이즈, ptr의 크기만큼 기존의 힙 늘리기, 만약 입력 사이즈가 기본 힙의 크기보다 작으면 그 나머지 메모리는 잘린다.
 * @param ptr  힙의 포인터
 * @param size 입력받은 사이즈
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;                                                             //oldptr에 기존 포인터 ptr을 저장
    void *newptr;                                                                   //크기를 늘린 뒤 ptr로 사용할 newptr 을 선언
    size_t copySize;                                                                //복사할 힙 크기인 coptysize 선언
    
    newptr = mm_malloc(size);                                                       //malloc 함수를 통해 입력된 size에 맞는 메모리 주소에 접근
    if (newptr == NULL)                                                             //오류시 null
      return NULL;
    copySize = GET_SIZE(HDRP(oldptr));                                              //기존에 있던 힙의 포인터가 가르키던 헤드의 size 를 copysize에 입력
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)                                                            //새로 할당하려는 데이터의 사이즈가 카피사이즈(malloc을 통해 배당된 주소의 사이즈 크기)보다 작으면
      copySize = size;                                                              //카피사이즈를 사이즈로 변경
                                                                                    //사이즈만큼만 데이터를 할당하고 copysize-size만큼은 공론
    memcpy(newptr, oldptr, copySize);                                               //newptr에 oldptr을 시작으로 copysize만큼 메모리 값 복사
    mm_free(oldptr);                                                                //기존 힙 반환
    return newptr;                                                                  //새로운 힙 리턴
}



                                                                //안되면 널이야 임마~

/**
 * 할당하려는 size 이상의 크기를 가진 가용 메모리 주소를 힙에서 찾아 반환한다.
 * @param asize malloc에서 DSIZE의 두배의 배수의 크기가 되도록 size를 확장시킨 값
 * next-fit이 선언되어 있으면 넥스트 핏을, 아닌 경우 first fit을 사용
*/
static void* find_fit(size_t asize){
    #ifdef NEXT_FIT
        void* bp;

        void* old_last_freep = last_freep;                                              // 마지막으로 사용했던 가용 블록의 주소를 'old~' 에  임시 저장한다.
        //for (최근 가용 상태로 만들었던함수 혹은 coalesce를 통해 합친 함수 부터 시작, 헤더에서 사이즈를 가져올 때까지, 다음 블록으로 bp를 옮기며 찾는다.)
        for (bp = last_freep; GET_SIZE(HDRP(bp)); bp = NEXT_BLKP(bp)){                  
            if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))                  //함수가 가용 상태이고, asize보다 size가 큰 블럭이 등장하면
                return bp;                                                              //멈춰!!!!
        }

        /*
        만약 끝까지 찾았는데도 안 나왔으면 처음부터 찾아본다.
        이 구문이 없으면 바로 extend_heap을 하는데, 
        이럼 앞에 있는 가용 블록들을 사용하지 못할 수 있어 메모리 낭비이다.
        */

       //for (프롤로그 헤더부터, bp가 최근 coalesce를 실행했던 함수까지, 다음 블록으로 bp를 옮기며 찾는다.)
        for (bp = heap_listp; bp < old_last_freep; bp = NEXT_BLKP(bp)){                 
            if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
                return bp;
        }

        last_freep = bp;  // 다시 탐색을 마친 시점으로 last_freep를 돌린다.

        return NULL;

		/* first-fit */
    #else
        void *bp;
        //for (프롤로그 블록에 bp 위치에서 시작해서,헤더에 기록된 블록의 사이즈가 0보다 클 때까지,다음 블록으로 bp 이동)
        for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp=NEXT_BLKP(bp))                
        {
            if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))                  //alloc이 0이고 사이즈가 get사이보다 작은 경우
            {
                return bp;                                                              //for문을 멈추고 bp값을 반환한다.
            }
        }
        return NULL;    
    #endif

}

/**
 * 
 * 블럭에 데이터를 지정하고 블럭의 남는 공간이 분할할 수 있을 만큼 여유가 있으면 분할함
 * 분할 할 수 있는 만큼 여유의 최소치 == 2*DSIZE
 * @param bp 분할하려는 블럭의 포인터
 * @param asize alloc에서 DSIZE의 두배의 배수의 크기가 되도록 size를 확장시킨 값
*/
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));                                              //분할하려는 블럭의 사이즈를 헤드에서 읽음

    if ((csize - asize) >= (2*DSIZE)){                                              //분할하려는 블럭의 사이즈와 asize의 값이 2*DSIZE일 때(분할할 수 있을 만큼 여유가 있을 때)
        PUT(HDRP(bp),PACK(asize,1));                                                //헤더에 사이즈 정보를 asize , alloc 을 1로 저장한다.
        PUT(FTRP(bp),PACK(asize,1));                                                //푸터에 사이즈 정보를 asize , alloc 을 1로 저장한다. (size 조절을 통해 분할) 
        bp = NEXT_BLKP(bp);                                                         //bp를 다음 블록으로 이동
        PUT(HDRP(bp),PACK(csize-asize,0));                                          //헤더에 사이즈 정보를 원래 블럭의 사이즈-asize로 분할
        PUT(FTRP(bp),PACK(csize-asize,0));                                          //푸터에 사이즈 정보를 원래 블럭의 사이즈-asize로 분할
    }
    else{
        PUT(HDRP(bp),PACK(csize,1));                                                //유지
        PUT(FTRP(bp),PACK(csize,1));                                                //유지
        }
}











