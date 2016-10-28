#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */

static struct semaphore *intersectionSem;
int volatile count = 0;
struct lock *intersectionLock;
struct cv *nocollision;//*empty;
int dirarray[4][4]={{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}};// NEWS*NEWS
/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */


int print_direction(Direction d);
//bool right_turn(Direction origin, Direction destination);
bool check_constraintss(Direction origin, Direction destination);
void adddir(Direction origin, Direction destination);
void subdir(Direction origin, Direction destination);

void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */

  intersectionSem = sem_create("intersectionSem",1);
  if (intersectionSem == NULL) {
    panic("could not create intersection semaphore");
  }
  intersectionLock = lock_create("intersectionLock");
  if (intersectionLock == NULL) {
    panic("could not create intersection lock");
  }
  nocollision = cv_create("checkfull");
  if (nocollision == NULL) {
    panic("could not create cv nocollision");
  }
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  KASSERT(intersectionSem != NULL);
  sem_destroy(intersectionSem);
  lock_destroy(intersectionLock);
  cv_destroy(nocollision);
  //cv_destroy(empty);
}



/*
 * check_constraints()
 * 
 * Purpose:
 *   checks whether the entry of a vehicle into the intersection violates
 *   any synchronization constraints.   Causes a kernel panic if so, otherwise
 *   returns silently
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * Returns:
 *   boolean
 */
int
print_direction(Direction d) {
  switch (d)
    {
    case north:
      return 0;
      break;
    case east:
      return 1;
      break;
    case west:
      return 2;
      break;
    case south:
      return 3;
      break;
    }
  return 4;
} 
bool
check_constraintss(Direction origin, Direction destination)  {
  /* compare newly-added vehicle to each other vehicles in in the intersection */
  int in = print_direction(origin);
  int out = print_direction(destination);
  //kprintf("%u %u %u\n",count,in,out);
  if((dirarray[in][out]>0)||count==0)return false;
  if((in==0 && out ==2)||(in==1 && out ==0)||
    (in==2 && out ==3)||(in==3 && out ==1)){
    for(int i=0;i<4;i++){
        if(dirarray[i][out]>0)return true;
    }
  }else{
    for(int i=0;i<4;i++){
      for(int j=0;j<4;j++){//if is 
        if((i==0 && j==2)||(i==1 && j ==0)||
          (i==2 && j ==3)||(i==3 && j ==1)){
          if(j==out && dirarray[i][j]>0)return true;
        }else{
          if(i!=in && dirarray[i][j]>0)return true;
        }
      }
    }
  }
  return false;
}


  /*
  if(count==0)return false;
  if(dirarray[in][0]>0 || dirarray[in][1]>0 || dirarray[in][2]>0 || dirarray[in][3]>0)return false;
  if(dirarray[out][in] > 0)return false;  
  bool right= ((in==0 && out==2)||(in==1 && out==0)||(in==2 && out==3)||(in==3 && out==1));
  //diff des and one is right turn
  if(right==true){
    if((dirarray[0][out]==0)&&(dirarray[1][out]==0)&&(dirarray[2][out]==0)&&(dirarray[3][out]==0))return false;
  }else{//check if is right turn
    for(int i=0;i<4;i++){
      if(dirarray[i][out]!=0)return true;
    }
    if((dirarray[0][1]==0) &&
       (dirarray[0][3]==0) &&
       (dirarray[1][2]==0) &&
       (dirarray[1][3]==0) &&
       (dirarray[2][0]==0) &&
       (dirarray[2][1]==0) &&
       (dirarray[3][0]==0) &&
       (dirarray[3][2]==0))
      return false;
  }
  */
  //kprintf("checking %u,%u,%u\n",count,in,out);

void adddir(Direction origin, Direction destination){
  int in = print_direction(origin);
  int out = print_direction(destination);
  dirarray[in][out] = dirarray[in][out] + 1;
}
void subdir(Direction origin, Direction destination){
  int in = print_direction(origin);
  int out = print_direction(destination);
  dirarray[in][out] = dirarray[in][out] - 1;
}
/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
  KASSERT(intersectionSem != NULL);
  //P(intersectionSem);
  lock_acquire(intersectionLock);
  while(check_constraintss(origin,destination)){
    cv_wait(nocollision,intersectionLock);
  }
  count = count + 1;
  adddir(origin, destination);
  //cv_broadcast(nocollision,intersectionLock);
  lock_release(intersectionLock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
  KASSERT(intersectionSem != NULL);
  //V(intersectionSem);
  lock_acquire(intersectionLock);
  //while(count == 0){
  //  cv_wait(nocollision,intersectionLock);
  //}
  count = count - 1;
  subdir(origin, destination);
  cv_broadcast(nocollision,intersectionLock);
  lock_release(intersectionLock);
}
