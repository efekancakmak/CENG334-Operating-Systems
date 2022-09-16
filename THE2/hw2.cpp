#include <iostream>
#include <vector>
#include <unordered_map>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include "hw2_output.h"

using namespace std;

// Structs Needed
typedef struct Cell{
    int y,x,amount;
    Cell(int a, int b, int c):y{a},x{b},amount{c} {}
} Cell;

typedef struct Private{
    int gid,si,sj,tg,ng;
    vector<Cell> areas;
    Private(int a, int b, int c, int d, int e):gid{a}, si{b}, sj{c}, tg{d}, ng{e} {}
} Private;

typedef struct Smoker{
    int sid,ts,ns;
    vector<Cell> areas;
    Smoker(int a, int b, int c):sid{a},ts{b},ns{c} {}
} Smoker;

typedef struct Order{
    string order;
    int time;
} Order;

// Function prototypes
void init_globals(void);
void *thread_private(void*);
void *thread_smoker(void*);
void time_calculation(timespec&, int&);

// COMMON GLOBALS
int N,M; // LENGTH, WIDTH
int pp_amount; // PROPER PRIVATE AMOUNT
int order_amount; // ORDER AMOUNT
int ss_amount; // SNEAKY SMOKER AMOUNT

unordered_map<int,Private*> Privates; // PID -> PP MAPPING
unordered_map<int,Smoker*> Smokers;   // SID -> SS MAPPING
vector<Order*> Orders;

int** grid; // TO HOLD CIGARETTE AMOUNTS..
int** inUse; // TO FETCH&SET USED AREAS..
int** inUse2; // TO FETCH&SET USED AREAS..
int which_part;

// SYNC GLOBALS
char state = 0; // 0 init, 1 gathering, 2 broken, 3 stop!
sem_t semInUse;
pthread_cond_t condForBreak;
pthread_mutex_t mutForBreak;
sem_t semInLitter;
pthread_cond_t condForInUse;
pthread_mutex_t mutForInUse;

void init_globals(){
    // INPUTS FOR PART 1
    cin >> N >> M;
    grid = new int*[N];
    for(int i=0; i<N; i++){
        grid[i] = new int[M];
        for(int j=0; j<M; j++)
            cin >> grid[i][j];
    }
    cin >> pp_amount;
    for(int i=0; i < pp_amount; i++){
        int temp[5];
        cin >> temp[0] >> temp[1] >> temp[2] >> temp[3] >> temp[4];
        Privates[temp[0]] = new Private(temp[0] , temp[1] , temp[2] , temp[3] , temp[4]);
        for(int j=0; j < temp[4]; j++){
            int xs,ys;
            cin >> ys >> xs;
            Privates[temp[0]]->areas.push_back(Cell(ys,xs,-1));
        }
    }
    // initially all areas are unlocked.
    inUse = new int*[N];
    inUse2 = new int*[N];
    for(int i=0; i<N; i++){
        inUse[i] = new int[M];
        inUse2[i] = new int[M];
        for(int j=0; j<M; j++){
            inUse[i][j] = 0;
            inUse2[i][j] = 0;
        }
    }
    // init semaphores to 1
    sem_init(&semInUse,0,1);
    sem_init(&semInLitter,0,1);
    which_part = 1;
    if(cin.eof())
        return;
    which_part = 2;
    // INPUTS FOR PART 2
    cin >> order_amount;
    for(int i=0; i < order_amount; i++){
        Order* tmp = new Order();
        cin >> tmp->time >> tmp->order;
        Orders.push_back(tmp);
    }
    if(cin.eof())
        return;
    which_part = 3;
    // INPUTS FOR PART 3
    cin >> ss_amount;
    for(int i=0; i < ss_amount; i++){
        int temp[3];
        cin >> temp[0] >> temp[1] >> temp[2];
        Smoker* tmp = new Smoker(temp[0],temp[1],temp[2]);
        Smokers[temp[0]] = tmp;
        for(int j=0; j<temp[2]; j++){
            int ty,tx,ta;
            cin >> ty >> tx >> ta;
            tmp->areas.push_back(Cell(ty,tx,ta));
        }
    }
}

void time_calculation(timespec& out, int& t){
    clock_gettime(CLOCK_REALTIME, &out);
    long xxx = out.tv_nsec + 1000000L*t;
    if (xxx >= 1000000000L){
        out.tv_nsec = (xxx)%1000000000L;
        out.tv_sec += (xxx)/1000000000L;
    }
    else
        out.tv_nsec = xxx;
}

void *thread_private(void* inp){
    int gid = *(int*)inp;
    hw2_notify(PROPER_PRIVATE_CREATED, gid, 0, 0);
    int starter = 0; // holds last clearing&cleared area.
    Private* mycv = Privates[gid];
    goto afterbreak;
    onbreak:
    hw2_notify(PROPER_PRIVATE_TOOK_BREAK, gid, 0, 0);
    pthread_mutex_lock(&mutForBreak);
    while ( state == 2 ){
        pthread_cond_wait(&condForBreak,&mutForBreak);
    }
    if (state == 3){
        pthread_mutex_unlock(&mutForBreak);
        hw2_notify(PROPER_PRIVATE_STOPPED, gid, 0, 0);
        return nullptr;
    }
    pthread_mutex_unlock(&mutForBreak);
    hw2_notify(PROPER_PRIVATE_CONTINUED, gid, 0, 0);
    afterbreak:
    int areas_to_clean = mycv->areas.size();
    for(int i=starter; i<areas_to_clean; i++){
        Cell* to_clean = &(mycv->areas[i]);
        goto coldstart;
    relook:
        pthread_mutex_lock(&mutForInUse);
        pthread_cond_wait(&condForInUse,&mutForInUse);
        pthread_mutex_unlock(&mutForInUse);

        pthread_mutex_lock(&mutForBreak);
        if (state==2){
            pthread_mutex_unlock(&mutForBreak);
            goto onbreak;
        }
        if (state == 3){
            pthread_mutex_unlock(&mutForBreak);
            hw2_notify(PROPER_PRIVATE_STOPPED, gid, 0, 0);
            return nullptr;
        }
        pthread_mutex_unlock(&mutForBreak);
    coldstart:
        sem_wait(&semInUse);
        for(int y=to_clean->y; y < to_clean->y + mycv->si; y++){
            for(int x=to_clean->x; x < to_clean->x + mycv->sj; x++){
                if (inUse[y][x] != 0){
                    sem_post(&semInUse);
                    goto relook;
                }
            }
        }
        // lock there!
        for(int y=to_clean->y; y < to_clean->y + mycv->si; y++){
            for(int x=to_clean->x; x < to_clean->x + mycv->sj; x++){
                inUse[y][x]=1;
            }
        }
        hw2_notify(PROPER_PRIVATE_ARRIVED, gid, to_clean->y, to_clean->x);
        sem_post(&semInUse);
            
        // start to gather
        for(int y=to_clean->y; y < to_clean->y + mycv->si; y++){
            for(int x=to_clean->x; x < to_clean->x + mycv->sj; x++){
                while(grid[y][x]>0){                        
                    struct timespec timeoutTime;
                    time_calculation(timeoutTime,mycv->tg);
                    pthread_mutex_lock(&mutForBreak);
                    pthread_cond_timedwait(&condForBreak,&mutForBreak,&timeoutTime);
                    if (state==2){
                        pthread_mutex_unlock(&mutForBreak);
                    doublecheck:
                        sem_wait(&semInUse);
                        for(int y=to_clean->y; y < to_clean->y + mycv->si; y++){
                            for(int x=to_clean->x; x < to_clean->x + mycv->sj; x++){
                                inUse[y][x]=0;
                            }
                        }
                        sem_post(&semInUse);
                        goto onbreak;
                    }
                    if (state==3){
                        pthread_mutex_unlock(&mutForBreak);
                        hw2_notify(PROPER_PRIVATE_STOPPED, gid, 0, 0);
                        return nullptr;
                    }
                    pthread_mutex_unlock(&mutForBreak);
                    pthread_mutex_lock(&mutForBreak);
                    if(state==2){
                        pthread_mutex_unlock(&mutForBreak);
                        goto doublecheck;
                    }
                    else
                        hw2_notify(PROPER_PRIVATE_GATHERED, gid, y, x);
                    pthread_mutex_unlock(&mutForBreak);
                    grid[y][x]--;
                }
            }
        }
        hw2_notify(PROPER_PRIVATE_CLEARED, gid, 0, 0);
        // unlock area
        sem_wait(&semInUse);
        for(int y=to_clean->y; y < to_clean->y + mycv->si; y++){
            for(int x=to_clean->x; x < to_clean->x + mycv->sj; x++){
                inUse[y][x]=0;
            }
        }
        sem_post(&semInUse);
        pthread_cond_broadcast(&condForInUse);
        starter++;
    }
   
    hw2_notify(PROPER_PRIVATE_EXITED, gid, 0, 0);
    return nullptr;
}

void *thread_smoker(void* inp){
    int sid = *(int*)inp;
    hw2_notify(SNEAKY_SMOKER_CREATED, sid, 0, 0);
    Smoker* mycv = Smokers[sid];
    int area_to_litter = mycv->areas.size();
    for(int i=0; i<area_to_litter; i++){
        Cell cell = mycv->areas[i];
        goto afterrelitter;
        relitter:
        pthread_mutex_lock(&mutForInUse);
        pthread_cond_wait(&condForInUse,&mutForInUse);
        pthread_mutex_unlock(&mutForInUse);
        pthread_mutex_lock(&mutForBreak);
        if (state==3){
            pthread_mutex_unlock(&mutForBreak);
            hw2_notify(SNEAKY_SMOKER_STOPPED, sid, 0, 0);
            return nullptr;
        }
        pthread_mutex_unlock(&mutForBreak);
        afterrelitter:
        sem_wait(&semInUse);
        bool inuse = false;
        if(inUse2[cell.y][cell.x]==1) {
            inuse = true;
        }
        for(int y=cell.y-1; (y < cell.y +1) && !inuse; y++){
            for(int x=cell.x-1; x < cell.x + 1; x++){
                if (inUse[y][x] == 1){
                    inuse = true;  
                    break;
                }      
            }
        }
        if(inuse){
            sem_post(&semInUse);
            goto relitter;
        }
        else{
            // fill inUse to show locking
            inUse2[cell.y][cell.x] = 1;
            for(int y=cell.y-1; y < cell.y +1; y++){
                for(int x=cell.x-1; x < cell.x + 1; x++){
                    inUse[y][x] += 2;
                }
            }
            sem_post(&semInUse);
            hw2_notify(SNEAKY_SMOKER_ARRIVED, sid, cell.y, cell.x);
        }

        int* ys = new int[8]{-1,-1,-1,0,1,1,1,0};
        int* xs = new int[8]{-1,0,1,1,1,0,-1,-1};
        int j=0;
        while(0 < cell.amount){//until smoking end..
            // here need waiting..
            struct timespec timeoutTime;
            time_calculation(timeoutTime,mycv->ts);
            pthread_mutex_lock(&mutForBreak);
            pthread_cond_timedwait(&condForBreak,&mutForBreak,&timeoutTime);
            if (state==3){
                pthread_mutex_unlock(&mutForBreak);
                hw2_notify(SNEAKY_SMOKER_STOPPED, sid, 0, 0);
                return nullptr;
            }
            pthread_mutex_unlock(&mutForBreak);
            sem_wait(&semInLitter);
            grid[cell.y + ys[j%8]][cell.x + xs[j%8]]++;
            sem_post(&semInLitter);
            hw2_notify(SNEAKY_SMOKER_FLICKED, sid, cell.y + ys[j%8], cell.x + xs[j%8]);
            cell.amount--;
            j++;
            
        }
        // unlock
        sem_wait(&semInUse);
        inUse2[cell.y][cell.x] = 0;
        for(int y=cell.y-1; y < cell.y +1; y++){
            for(int x=cell.x-1; x < cell.x + 1; x++){
                inUse[y][x] -= 2;
            }
        }
        sem_post(&semInUse);
        hw2_notify(SNEAKY_SMOKER_LEFT, sid, 0, 0);
        pthread_cond_broadcast(&condForInUse);

    }
    hw2_notify(SNEAKY_SMOKER_EXITED, sid, 0, 0);
    return nullptr;
}

int main(void){
    init_globals(); // also takes inputs
    hw2_init_notifier();
    // init private threads
    pthread_t pts[pp_amount];
    int ii = 0;
    for(auto p : Privates)
        pthread_create(&pts[ii++],NULL,thread_private,&p.second->gid);
    // init smoker threads
    pthread_t sts[ss_amount];
    ii = 0;
    if (which_part==3)
        for(auto s : Smokers)
            pthread_create(&sts[ii++],NULL,thread_smoker,&s.second->sid);
    // deal with orders..;
    int order_state = 1; //initially working.. broken = 2, stopped = 3;
    int before = 0;
    for(int i=0; i < order_amount; i++){
        usleep(1000*(Orders[i]->time-before));
        pthread_mutex_lock(&mutForBreak);
        if (Orders[i]->order == "break"){
            hw2_notify(ORDER_BREAK,0, 0, 0);
            if(order_state==1){
                state = 2;
                order_state = 2;
            }
        }
        else if (Orders[i]->order == "continue"){
            hw2_notify(ORDER_CONTINUE, 0, 0, 0);
            if(order_state==2){
                state = 1;
                order_state = 1;
            }
        }
        else if(Orders[i]->order == "stop"){
            hw2_notify(ORDER_STOP, 0, 0, 0);
            state = 3;
            order_state = 3;
        }
        pthread_mutex_unlock(&mutForBreak);
        pthread_cond_broadcast(&condForBreak);
        pthread_cond_broadcast(&condForInUse);
        before = Orders[i]->time;
    }
    // wait privates end..
    for(int i=0; i < pp_amount; i++)
        pthread_join(pts[i], NULL);
    // wait smokers end..
    if (which_part==3)
        for(int i=0; i < ss_amount; i++)
            pthread_join(sts[i], NULL);
    return 0;
}