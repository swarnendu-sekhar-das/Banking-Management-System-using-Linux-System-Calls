/*
                                        Design and Development of Banking Management System

                                                    Name - Swarnendu Sekhar Das
                                                        Roll No. - MT2025125

                                            Banking Management System - Server Side Code
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/file.h>
#include <pthread.h>
#include <time.h>

#define PORT 8080
#define BUF 1024
#define STR 50

#define CUST 1
#define EMP 2
#define MGR 3
#define ADM 4

typedef struct { int id; char user[STR], pass[STR], name[STR]; double bal; int active, acc; } Cust;
typedef struct { int id; char user[STR], pass[STR], name[STR]; int role; } Emp;
typedef struct { int id, cid; char type[20]; double amt, bal; char time[30]; int to; } Trans;
typedef struct { int id, cid; double amt; char status[20]; int emp; } Loan;
typedef struct { int id, cid; char text[256]; int rev; } Feed;
typedef struct { char user[STR]; int active; } Sess;
static pthread_mutex_t sess_mutex = PTHREAD_MUTEX_INITIALIZER;

void* session_handler(void*);
void send_msg(int, const char*);
int get_input(int, char*, int);

int main() {
    int s, c, opt = 1;
    struct sockaddr_in addr = {AF_INET, htons(PORT), {INADDR_ANY}};
    pthread_t t;
    
    // Creating and initializing data files
    int fd = open("emp.dat", O_CREAT|O_RDWR, 0666);
    if(lseek(fd, 0, SEEK_END) == 0) {
        Emp a = {1, "admin", "admin123", "Admin", ADM};
        write(fd, &a, sizeof(Emp));
    }
    close(fd);
    close(open("cust.dat", O_CREAT|O_RDWR, 0666));
    close(open("trans.dat", O_CREAT|O_RDWR, 0666));
    close(open("loan.dat", O_CREAT|O_RDWR, 0666));
    close(open("feed.dat", O_CREAT|O_RDWR, 0666));
    close(open("sess.dat", O_CREAT|O_RDWR, 0666));
    
    // Creating server socket
    s = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(s, (struct sockaddr*)&addr, sizeof(addr));
    listen(s, 10);
    printf("Server started on port %d\n", PORT);
    
    // Accepting client connections
    while(1) {
        c = accept(s, NULL, NULL);
        int *pc = malloc(sizeof(int)); *pc = c;
        pthread_create(&t, NULL, session_handler, pc);
        pthread_detach(t);
    }
}

// Authentication - Checks Customer and Employee Files
int auth(char* u, char* p, int* role, int* id) {
    Cust cu; Emp e; int fd;
    
    // Check in Customers File
    if((fd = open("cust.dat", O_RDONLY)) >= 0) {
        flock(fd, LOCK_SH);
        while(read(fd, &cu, sizeof(Cust)) == sizeof(Cust)) {
            if(!strcmp(cu.user, u) && !strcmp(cu.pass, p) && cu.active) {
                *role = CUST; *id = cu.id;
                flock(fd, LOCK_UN); close(fd);
                return 1;
            }
        }
        flock(fd, LOCK_UN); close(fd);
    }
    
    // Check in Employees File
    if((fd = open("emp.dat", O_RDONLY)) >= 0) {
        flock(fd, LOCK_SH);
        while(read(fd, &e, sizeof(Emp)) == sizeof(Emp)) {
            if(!strcmp(e.user, u) && !strcmp(e.pass, p)) {
                *role = e.role; *id = e.id;
                flock(fd, LOCK_UN); close(fd);
                return 1;
            }
        }
        flock(fd, LOCK_UN); close(fd);
    }
    return 0;
}

// Session Management
int check_sess(char* u) {
    int fd = open("sess.dat", O_RDONLY); if(fd < 0) return 0;
    Sess s; flock(fd, LOCK_SH);
    while(read(fd, &s, sizeof(Sess)) == sizeof(Sess))
        if(!strcmp(s.user, u) && s.active) { flock(fd, LOCK_UN); close(fd); return 1; }
    flock(fd, LOCK_UN); close(fd); return 0;
}

void make_sess(char* u) {
    int fd = open("sess.dat", O_WRONLY|O_CREAT|O_APPEND, 0666);
    Sess s; memset(&s, 0, sizeof(Sess)); strcpy(s.user, u); s.active = 1;
    flock(fd, LOCK_EX); write(fd, &s, sizeof(Sess)); flock(fd, LOCK_UN); close(fd);
}

void del_sess(char* u) {
    int fd = open("sess.dat", O_RDWR); if(fd < 0) return;
    Sess s; int off = 0; flock(fd, LOCK_EX);
    while(read(fd, &s, sizeof(Sess)) == sizeof(Sess)) {
        if(!strcmp(s.user, u) && s.active) {
            s.active = 0; lseek(fd, off, SEEK_SET); write(fd, &s, sizeof(Sess)); break;
        }
        off += sizeof(Sess);
    }
    flock(fd, LOCK_UN); close(fd);
}

// Required functions
void send_msg(int c, const char *m) {
    ssize_t n = send(c, m, strlen(m), 0);
    if (n <= 0) {
        close(c);
        pthread_exit(NULL);
    }
}

int get_input(int c, char* b, int sz) {
    ssize_t n = recv(c, b, sz - 1, 0);
    if (n <= 0) {
        close(c);
        pthread_exit(NULL);
    }
    b[n] = '\0';
    b[strcspn(b, "\r\n")] = '\0'; 
    return (int)n;
}

void get_time(char* b) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(b, 30, "%Y-%m-%d %H:%M:%S", tm);
}

static void clean_sess(void *arg) {
    char *user_heap = (char*)arg;
    if(user_heap) {
        del_sess(user_heap);   
        free(user_heap);      
    }
}

// Change password
void chg_pass(int c, int uid, int role) {
    char buf[BUF], np[STR];
    send_msg(c, "New Password: "); get_input(c, np, STR);
    
    if(role == CUST) {
        int fd = open("cust.dat", O_RDWR); Cust cu; int off = 0;
        flock(fd, LOCK_EX);
        while(read(fd, &cu, sizeof(Cust)) == sizeof(Cust)) {
            if(cu.id == uid) {
                strcpy(cu.pass, np);
                lseek(fd, off, SEEK_SET); write(fd, &cu, sizeof(Cust)); break;
            }
            off += sizeof(Cust);
        }
        flock(fd, LOCK_UN); close(fd);
    } else {
        int fd = open("emp.dat", O_RDWR); Emp e; int off = 0;
        flock(fd, LOCK_EX);
        while(read(fd, &e, sizeof(Emp)) == sizeof(Emp)) {
            if(e.id == uid) {
                strcpy(e.pass, np);
                lseek(fd, off, SEEK_SET); write(fd, &e, sizeof(Emp)); break;
            }
            off += sizeof(Emp);
        }
        flock(fd, LOCK_UN); close(fd);
    }
    send_msg(c, "Password Changed!\n");
}

// CUSTOMER MENU 
void cust_menu(int c, int uid) {
    char buf[BUF];
    char menu[300];
    sprintf(menu, "\n=== Customer (ID: %d) ===\n1.View Balance \n2.Deposit \n3.Withdraw \n4.Transfer \n5.Apply Loan \n6.Check Loan Status \n7.Transaction History \n8.Give Feedback \n9.Change Password \n10.Logout\n> ", uid);
    
    while(1) {
        send_msg(c, menu); get_input(c, buf, BUF); int ch = atoi(buf);
        
        // View Balance
        if(ch == 1) {
            int fd = open("cust.dat", O_RDONLY); Cust cu;
            flock(fd, LOCK_SH);
            while(read(fd, &cu, sizeof(Cust)) == sizeof(Cust))
                if(cu.id == uid) { sprintf(buf, "Acc:%d Bal:%.2f\n", cu.acc, cu.bal); send_msg(c, buf); break; }
            flock(fd, LOCK_UN); close(fd);
        }
        // Deposit or Withdraw Money
        else if(ch == 2 || ch == 3) {
            send_msg(c, "Amount: "); get_input(c, buf, BUF); double amt = atof(buf);
            if(amt <= 0) { send_msg(c, "Invalid Amount!\n"); continue; }
            
            int fd = open("cust.dat", O_RDWR); Cust cu; int off = 0, ok = 0;
            flock(fd, LOCK_EX);
            while(read(fd, &cu, sizeof(Cust)) == sizeof(Cust)) {
                if(cu.id == uid) {
                    if(ch == 2) { cu.bal += amt; ok = 1; }
                    else if(cu.bal >= amt) { cu.bal -= amt; ok = 1; }
                    else { send_msg(c, "Insufficient Funds!\n"); break; }
                    
                    if(ok) {
                        lseek(fd, off, SEEK_SET); write(fd, &cu, sizeof(Cust));
                        // Log Transactions
                        Trans t; memset(&t, 0, sizeof(Trans));
                        t.cid = uid; strcpy(t.type, ch==2?"DEPOSIT":"WITHDRAW");
                        t.amt = amt; t.bal = cu.bal; get_time(t.time);
                        int tfd = open("trans.dat", O_WRONLY|O_CREAT|O_APPEND, 0666);
                        t.id = lseek(tfd, 0, SEEK_END)/sizeof(Trans)+1;
                        write(tfd, &t, sizeof(Trans)); close(tfd);
                        sprintf(buf, "Done! Bal:%.2f\n", cu.bal); send_msg(c, buf);
                    }
                    break;
                }
                off += sizeof(Cust);
            }
            flock(fd, LOCK_UN); close(fd);
        }

        // Transfer Funds
        else if(ch == 4) {
            send_msg(c, "To Account: "); get_input(c, buf, BUF);
            int to = atoi(buf);

            send_msg(c, "Amount: "); get_input(c, buf, BUF);
            double amt = atof(buf);
            if(amt <= 0) { send_msg(c, "Invalid!\n"); continue; }

            int cfd = open("cust.dat", O_RDWR);
            int tfd = open("trans.dat", O_WRONLY|O_CREAT|O_APPEND, 0666);
            if(cfd < 0 || tfd < 0) {
                if(cfd >= 0) close(cfd);
                if(tfd >= 0) close(tfd);
                send_msg(c, "Error opening data files!\n");
                continue;
            }

            flock(cfd, LOCK_EX);
            flock(tfd, LOCK_EX);

            Cust from, toc;
            int foff = 0, toff = 0;
            int ff = 0, tf = 0;

            lseek(cfd, 0, SEEK_SET);
            while(read(cfd, &from, sizeof(Cust)) == sizeof(Cust)) {
                if(from.id == uid) { ff = 1; break; }
                foff += sizeof(Cust);
            }

            lseek(cfd, 0, SEEK_SET);
            toff = 0;
            while(read(cfd, &toc, sizeof(Cust)) == sizeof(Cust)) {
                if(toc.acc == to && toc.active) { tf = 1; break; }
                toff += sizeof(Cust);
            }

            if(!ff || !tf || from.bal < amt) {
                flock(tfd, LOCK_UN); 
                close(tfd);
                flock(cfd, LOCK_UN); 
                close(cfd);
                send_msg(c, "Transfer Failed! Check Balance or Account Number.\n");
                continue;
            }

            from.bal -= amt;
            toc.bal += amt;

            lseek(cfd, foff, SEEK_SET); 
            write(cfd, &from, sizeof(Cust));
            lseek(cfd, toff, SEEK_SET); 
            write(cfd, &toc, sizeof(Cust));

            Trans t1, t2;
            memset(&t1, 0, sizeof(Trans));
            memset(&t2, 0, sizeof(Trans));

            t1.cid = from.id; strcpy(t1.type, "TRANSFER OUT");
            t1.amt = amt; t1.bal = from.bal; t1.to = toc.acc;

            t2.cid = toc.id; strcpy(t2.type, "TRANSFER IN");
            t2.amt = amt; t2.bal = toc.bal; t2.to = from.acc;

            get_time(t1.time);
            strcpy(t2.time, t1.time);

            off_t cur = lseek(tfd, 0, SEEK_END);
            t1.id = (cur / sizeof(Trans)) + 1;
            write(tfd, &t1, sizeof(Trans));

            cur = lseek(tfd, 0, SEEK_END);
            t2.id = (cur / sizeof(Trans)) + 1;
            write(tfd, &t2, sizeof(Trans));

            fsync(tfd);
            fsync(cfd);

            flock(tfd, LOCK_UN); close(tfd);
            flock(cfd, LOCK_UN); close(cfd);

            sprintf(buf, "Transfer Successful! Your Balance: %.2f\n", from.bal);
            send_msg(c, buf);
        }

        // Apply for Loan
        else if(ch == 5) {
            send_msg(c, "Amount: "); get_input(c, buf, BUF); double amt = atof(buf);
            if(amt <= 0) { send_msg(c, "Invalid!\n"); continue; }
            
            Loan l; memset(&l, 0, sizeof(Loan));
            l.cid = uid; l.amt = amt; strcpy(l.status, "PENDING"); l.emp = 0;
            int fd = open("loan.dat", O_WRONLY|O_CREAT|O_APPEND, 0666);
            flock(fd, LOCK_EX); l.id = lseek(fd, 0, SEEK_END)/sizeof(Loan)+1;
            write(fd, &l, sizeof(Loan)); flock(fd, LOCK_UN); close(fd);
            sprintf(buf, "Loan Applied! ID:%d\n", l.id); send_msg(c, buf);
        }
        // View Loan Status
        else if(ch == 6) {
            int fd = open("loan.dat", O_RDONLY); Loan l; send_msg(c, "\n=== Loans ===\n");
            flock(fd, LOCK_SH);
            while(read(fd, &l, sizeof(Loan)) == sizeof(Loan)) {
                if(l.cid == uid) {
                    sprintf(buf, "ID:%d %.2f %s Emp:%d\n", l.id, l.amt, l.status, l.emp);
                    send_msg(c, buf);
                }
            }
            flock(fd, LOCK_UN); close(fd);
        }
        // View Transaction History
        else if(ch == 7) {
            int fd = open("trans.dat", O_RDONLY); Trans t; send_msg(c, "\n=== Transaction History ===\n");
            flock(fd, LOCK_SH);
            while(read(fd, &t, sizeof(Trans)) == sizeof(Trans)) {
                if(t.cid == uid) {
                    sprintf(buf, "%s %.2f Bal:%.2f %s\n", t.type, t.amt, t.bal, t.time);
                    send_msg(c, buf);
                }
            }
            flock(fd, LOCK_UN); close(fd);
        }
        // Add Feedback
        else if(ch == 8) {
            send_msg(c, "Feedback: "); get_input(c, buf, BUF);
            Feed f; memset(&f, 0, sizeof(Feed)); f.cid = uid; strcpy(f.text, buf); f.rev = 0;
            int fd = open("feed.dat", O_WRONLY|O_CREAT|O_APPEND, 0666);
            flock(fd, LOCK_EX); f.id = lseek(fd, 0, SEEK_END)/sizeof(Feed)+1;
            write(fd, &f, sizeof(Feed)); flock(fd, LOCK_UN); close(fd);
            send_msg(c, "Submitted!\n");
        }
        // Change Password
        else if(ch == 9) chg_pass(c, uid, CUST);
        // Logout from Session
        else if(ch == 10) return;
    }
}

// EMPLOYEE MENU 
void emp_menu(int c, int uid) {
    char buf[BUF];
    sprintf(buf, "\n=== Employee (ID:%d) ===\n1.Add Customer \n2.Modify Customer \n3.View Loans \n4.Process Loans \n5.View Transactions \n6.Change Password \n7.Logout\n> ", uid);
    
    while(1) {
        send_msg(c, buf); get_input(c, buf, BUF); int ch = atoi(buf);
        
        // Add Customer
        if(ch == 1) {
            Cust cu; memset(&cu, 0, sizeof(Cust));
            send_msg(c, "User: "); get_input(c, cu.user, STR);
            send_msg(c, "Pass: "); get_input(c, cu.pass, STR);
            send_msg(c, "Name: "); get_input(c, cu.name, STR);
            send_msg(c, "Balance: "); get_input(c, buf, BUF); cu.bal = atof(buf);
            cu.active = 1;
            
            int fd = open("cust.dat", O_WRONLY|O_CREAT|O_APPEND, 0666);
            flock(fd, LOCK_EX);
            cu.id = lseek(fd, 0, SEEK_END)/sizeof(Cust)+1;
            cu.acc = 1000+cu.id;
            write(fd, &cu, sizeof(Cust));
            flock(fd, LOCK_UN); close(fd);
            sprintf(buf, "Added! Account Number:%d\n", cu.acc); send_msg(c, buf);
            sprintf(buf, "\n=== Employee (ID:%d) ===\n1.Add Customer \n2.Modify Customer \n3.View Loans \n4.Process Loans \n5.View Transaction \n6.Change Password \n7.Logout\n> ", uid);
        }
        // Modify customer
        else if(ch == 2) {
            send_msg(c, "Acc: "); get_input(c, buf, BUF); int acc = atoi(buf);
            int fd = open("cust.dat", O_RDWR); Cust cu; int off=0, found=0;
            flock(fd, LOCK_EX);
            while(read(fd, &cu, sizeof(Cust)) == sizeof(Cust)) {
                if(cu.acc == acc) {
                    found = 1;
                    send_msg(c, "Name: "); get_input(c, buf, BUF); if(strlen(buf)) strcpy(cu.name, buf);
                    send_msg(c, "Pass: "); get_input(c, buf, BUF); if(strlen(buf)) strcpy(cu.pass, buf);
                    lseek(fd, off, SEEK_SET); write(fd, &cu, sizeof(Cust)); break;
                }
                off += sizeof(Cust);
            }
            flock(fd, LOCK_UN); close(fd);
            send_msg(c, found ? "Updated!\n" : "Not found!\n");
            sprintf(buf, "\n=== Employee (ID:%d) ===\n1.Add Customer \n2.Modify Customer \n3.View Loans \n4.Process Loans \n5.View Transaction \n6.Change Password \n7.Logout\n> ", uid);
        }
        // View assigned loans
        else if(ch == 3) {
            int fd = open("loan.dat", O_RDONLY); 
            if(fd < 0) {
                send_msg(c, "Error opening loan file\n");
                sprintf(buf, "\n=== Employee (ID:%d) ===\n1.Add Customer \n2.Modify Customer \n3.View Loans \n4.Process Loans \n5.View Transaction \n6.Change Password \n7.Logout\n> ", uid);
                continue;
            }
            
            Loan l; 
            char msg[BUF];
            sprintf(msg, "\n=== Assigned Loans (Employee ID:%d) ===\n", uid);
            send_msg(c, msg);
            
            int count = 0;
            flock(fd, LOCK_SH);
            while(read(fd, &l, sizeof(Loan)) == sizeof(Loan)) {
                if(l.emp == uid) {
                    sprintf(msg, "LoanID:%d | CustomerID:%d | Amount:%.2f | Status:%s\n", 
                            l.id, l.cid, l.amt, l.status);
                    send_msg(c, msg);
                    count++;
                }
            }
            flock(fd, LOCK_UN); 
            close(fd);
            
            if(count == 0) {
                send_msg(c, "No loans assigned yet\n");
            } else {
                sprintf(msg, "Total assigned: %d loan(s)\n", count);
                send_msg(c, msg);
            }
            sprintf(buf, "\n=== Employee (ID:%d) ===\n1.Add Customer \n2.Modify Customer \n3.View Loans \n4.Process Loans \n5.View Transaction \n6.Change Password \n7.Logout\n> ", uid);
        }
        // Process loan
        else if(ch == 4) {
            int fd = open("loan.dat", O_RDONLY); 
            if(fd < 0) {
                send_msg(c, "Error opening loan file\n");
                sprintf(buf, "\n=== Employee (ID:%d) ===\n1.Add Customer \n2.Modify Customer \n3.View Loans \n4.Process Loans \n5.View Transaction \n6.Change Password \n7.Logout\n> ", uid);
                continue;
            }
            
            Loan l; 
            char msg[BUF];
            sprintf(msg, "\n=== Pending Loans (Employee ID:%d) ===\n", uid);
            send_msg(c, msg);
            
            int cnt = 0;
            flock(fd, LOCK_SH);
            while(read(fd, &l, sizeof(Loan)) == sizeof(Loan)) {
                if(l.emp == uid && !strcmp(l.status, "PENDING")) {
                    sprintf(msg, "LoanID:%d | CustomerID:%d | Amount:%.2f\n", l.id, l.cid, l.amt);
                    send_msg(c, msg); 
                    cnt++;
                }
            }
            flock(fd, LOCK_UN); 
            close(fd);
            
            if(!cnt) { 
                send_msg(c, "No pending loans to process\n"); 
                sprintf(buf, "\n=== Employee (ID:%d) ===\n1.Add Customer \n2.Modify \nCustomer \n3.View Loans \n4.Process Loans \n5.View Transaction \n6.Change Password \n7.Logout\n> ", uid);
                continue; 
            }
            
            send_msg(c, "\nLoan ID: "); get_input(c, buf, BUF); int lid = atoi(buf);
            send_msg(c, "1.Approve 2.Reject: "); get_input(c, buf, BUF); int act = atoi(buf);
            
            fd = open("loan.dat", O_RDWR); 
            if(fd < 0) {
                send_msg(c, "Error opening loan file\n");
                sprintf(buf, "\n=== Employee (ID:%d) ===\n1.Add Customer \n2.Modify \nCustomer \n3.View Loans \n4.Process Loans \n5.View Transaction \n6.Change Password \n7.Logout\n> ", uid);
                continue;
            }
            
            int off = 0, found = 0;
            flock(fd, LOCK_EX);
            while(read(fd, &l, sizeof(Loan)) == sizeof(Loan)) {
                if(l.id == lid && l.emp == uid) {
                    found = 1;
                    strcpy(l.status, act==1?"APPROVED":"REJECTED");
                    lseek(fd, off, SEEK_SET); 
                    write(fd, &l, sizeof(Loan)); 
                    break;
                }
                off += sizeof(Loan);
            }
            flock(fd, LOCK_UN); 
            close(fd);
            
            if(found) {
                sprintf(msg, "Loan %d %s successfully!\n", lid, act==1?"APPROVED":"REJECTED");
                send_msg(c, msg);
            } else {
                send_msg(c, "Loan not found or not assigned to you!\n");
            }
            sprintf(buf, "\n=== Employee (ID:%d) ===\n1.Add Customer \n2.Modify Customer \n3.View Loans \n4.Process Loans \n5.View Transaction \n6.Change Password \n7.Logout\n> ", uid);
        }
        // View customer transactions
        else if(ch == 5) {
            send_msg(c, "Cust ID: "); get_input(c, buf, BUF); int cid = atoi(buf);
            int fd = open("trans.dat", O_RDONLY); 
            if(fd < 0) {
                send_msg(c, "No transactions found\n");
                sprintf(buf, "\n=== Employee (ID:%d) ===\n1.Add Customer \n2.Modify Customer \n3.View Loans \n4.Process Loans \n5.View Transaction \n6.Change Password \n7.Logout\n> ", uid);
                continue;
            }
            
            Trans t; 
            char msg[BUF];
            send_msg(c, "\n=== Customer Transactions ===\n");
            
            flock(fd, LOCK_SH);
            while(read(fd, &t, sizeof(Trans)) == sizeof(Trans)) {
                if(t.cid == cid) {
                    sprintf(msg, "%s %.2f Balance:%.2f %s\n", t.type, t.amt, t.bal, t.time);
                    send_msg(c, msg);
                }
            }
            flock(fd, LOCK_UN); 
            close(fd);
            sprintf(buf, "\n=== Employee (ID:%d) ===\n1.Add Customer \n2.Modify Customer \n3.View Loans \n4.Process Loans \n5.View Transaction \n6.Change Password \n7.Logout\n> ", uid);
        }
        // Change password
        else if(ch == 6) {
            chg_pass(c, uid, EMP);
            sprintf(buf, "\n=== Employee (ID:%d) ===\n1.Add Customer \n2.Modify Customer \n3.View Loans \n4.Process Loans \n5.View Transaction \n6.Change Password \n7.Logout\n> ", uid);
        }
        // Logout
        else if(ch == 7) return;
    }
}

// MANAGER MENU 
void mgr_menu(int c, int uid) {
    char buf[BUF];
    char menu[250];
    sprintf(menu, "\n=== Manager (ID: %d) ===\n1.Activate/Deactivate Account \n2.Assign Loan \n3.Review Feedback \n4.Change Password \n5.Logout\n> ", uid);
    
    while(1) {
        send_msg(c, menu); get_input(c, buf, BUF); int ch = atoi(buf);
        
        // Activate/Deactivate account
        if(ch == 1) {
            send_msg(c, "Account: "); get_input(c, buf, BUF); int acc = atoi(buf);
            send_msg(c, "1.Activate 2.Deactivate: "); get_input(c, buf, BUF); int act = atoi(buf);
            
            int fd = open("cust.dat", O_RDWR); Cust cu; int off = 0;
            flock(fd, LOCK_EX);
            while(read(fd, &cu, sizeof(Cust)) == sizeof(Cust)) {
                if(cu.acc == acc) {
                    cu.active = (act==1); lseek(fd, off, SEEK_SET);
                    write(fd, &cu, sizeof(Cust)); break;
                }
                off += sizeof(Cust);
            }
            flock(fd, LOCK_UN); close(fd);
            send_msg(c, "Done!\n");
        }
        // Assign loan to employee
        else if(ch == 2) {
            int fd = open("loan.dat", O_RDONLY); Loan l; send_msg(c, "\n=== Unassigned ===\n");
            int cnt = 0;
            flock(fd, LOCK_SH);
            while(read(fd, &l, sizeof(Loan)) == sizeof(Loan)) {
                if(!strcmp(l.status, "PENDING") && l.emp == 0) {
                    sprintf(buf, "ID:%d Cust:%d %.2f\n", l.id, l.cid, l.amt);
                    send_msg(c, buf); cnt++;
                }
            }
            flock(fd, LOCK_UN); close(fd);
            
            if(!cnt) { send_msg(c, "None\n"); continue; }
            
            send_msg(c, "Loan ID: "); get_input(c, buf, BUF); int lid = atoi(buf);
            send_msg(c, "Emp ID: "); get_input(c, buf, BUF); int eid = atoi(buf);
            
            fd = open("loan.dat", O_RDWR); int off = 0;
            flock(fd, LOCK_EX);
            while(read(fd, &l, sizeof(Loan)) == sizeof(Loan)) {
                if(l.id == lid) {
                    l.emp = eid; lseek(fd, off, SEEK_SET);
                    write(fd, &l, sizeof(Loan)); break;
                }
                off += sizeof(Loan);
            }
            flock(fd, LOCK_UN); close(fd);
            send_msg(c, "Assigned!\n");
        }
        else if(ch == 3) {
            int fd = open("feed.dat", O_RDWR); Feed f; int off = 0;
            send_msg(c, "\n=== Feedback ===\n");
            flock(fd, LOCK_EX);
            while(read(fd, &f, sizeof(Feed)) == sizeof(Feed)) {
                sprintf(buf, "ID:%d Cust:%d %s\n", f.id, f.cid, f.text);
                send_msg(c, buf);
                if(!f.rev) {
                    f.rev = 1; lseek(fd, off, SEEK_SET); write(fd, &f, sizeof(Feed));
                    lseek(fd, off+sizeof(Feed), SEEK_SET);
                }
                off += sizeof(Feed);
            }
            flock(fd, LOCK_UN); close(fd);
        }
        // Change password
        else if(ch == 4) chg_pass(c, uid, MGR);
        // Logout
        else if(ch == 5) return;
    }
}

// ADMIN MENU 
void adm_menu(int c, int uid) {
    char buf[BUF];
    char menu[250];
    sprintf(menu, "\n=== Admin (ID: %d)===\n1.Add Employee \n2.Modify Employee Data \n3.Manage Roles \n4.Change Password \n5.Logout\n> ", uid);
    
    while(1) {
        send_msg(c, menu); get_input(c, buf, BUF); int ch = atoi(buf);
        
        // Add employee/manager/admin
        if(ch == 1) {
            Emp e; memset(&e, 0, sizeof(Emp));
            send_msg(c, "Username: "); get_input(c, e.user, STR);
            send_msg(c, "Password: "); get_input(c, e.pass, STR);
            send_msg(c, "Name: "); get_input(c, e.name, STR);
            send_msg(c, "Role (2=Employee, 3=Manager, 4=Admin): "); 
            get_input(c, buf, BUF); 
            e.role = atoi(buf);
            
            if(e.role < 2 || e.role > 4) { 
                send_msg(c, "Invalid role! Input should be 2, 3, or 4\n"); 
                continue; 
            }
            
            int fd = open("emp.dat", O_WRONLY|O_CREAT|O_APPEND, 0666);
            flock(fd, LOCK_EX);
            e.id = lseek(fd, 0, SEEK_END)/sizeof(Emp)+1;
            write(fd, &e, sizeof(Emp));
            flock(fd, LOCK_UN); close(fd);
            
            char role_name[20];
            if(e.role == 2) strcpy(role_name, "Employee");
            else if(e.role == 3) strcpy(role_name, "Manager");
            else if(e.role == 4) strcpy(role_name, "Admin");
            
            sprintf(buf, "%s Successfully Added! ID: %d\n", role_name, e.id);
            send_msg(c, buf);
        }
        // Modify employee
        else if(ch == 2) {
            send_msg(c, "Emp ID: "); get_input(c, buf, BUF); int eid = atoi(buf);
            int fd = open("emp.dat", O_RDWR); Emp e; int off = 0, found = 0;
            flock(fd, LOCK_EX);
            while(read(fd, &e, sizeof(Emp)) == sizeof(Emp)) {
                if(e.id == eid) {
                    found = 1;
                    
                    char current[BUF];
                    char role_name[20];
                    if(e.role == 2) strcpy(role_name, "Employee");
                    else if(e.role == 3) strcpy(role_name, "Manager");
                    else if(e.role == 4) strcpy(role_name, "Admin");
                    
                    sprintf(current, "Current: %s (%s)\n", e.name, role_name);
                    send_msg(c, current);
                    
                    send_msg(c, "New Name (enter to skip): "); 
                    get_input(c, buf, BUF); 
                    if(strlen(buf)) strcpy(e.name, buf);
                    
                    send_msg(c, "New Password (enter to skip): "); 
                    get_input(c, buf, BUF); 
                    if(strlen(buf)) strcpy(e.pass, buf);
                    
                    lseek(fd, off, SEEK_SET); 
                    write(fd, &e, sizeof(Emp)); 
                    break;
                }
                off += sizeof(Emp);
            }
            flock(fd, LOCK_UN); close(fd);
            send_msg(c, found ? "Details updated!\n" : "Employee not found!\n");
        }
        // Manage user roles
        else if(ch == 3) {
            send_msg(c, "Emp ID: "); get_input(c, buf, BUF); int eid = atoi(buf);
            send_msg(c, "New Role (2=Employee, 3=Manager, 4=Admin): "); 
            get_input(c, buf, BUF); 
            int role = atoi(buf);
            
            if(role < 1 || role > 3) { 
                send_msg(c, "Invalid role! Input should be 2, 3, or 4\n"); 
                continue; 
            }
            
            int fd = open("emp.dat", O_RDWR); Emp e; int off = 0, found = 0;
            flock(fd, LOCK_EX);
            while(read(fd, &e, sizeof(Emp)) == sizeof(Emp)) {
                if(e.id == eid) {
                    found = 1;
                    int old_role = e.role;
                    e.role = role;
                    lseek(fd, off, SEEK_SET);
                    write(fd, &e, sizeof(Emp));
                    
                    char old_name[20], new_name[20];
                    if(old_role == 2) strcpy(old_name, "Employee");
                    else if(old_role == 3) strcpy(old_name, "Manager");
                    else if(old_role == 4) strcpy(old_name, "Admin");
                    
                    if(role == 2) strcpy(new_name, "Employee");
                    else if(role == 3) strcpy(new_name, "Manager");
                    else if(role == 4) strcpy(new_name, "Admin");
                    
                    sprintf(buf, "Role changed: %s → %s\n", old_name, new_name);
                    send_msg(c, buf);
                    break;
                }
                off += sizeof(Emp);
            }
            flock(fd, LOCK_UN); close(fd);
            
            if(!found) send_msg(c, "Employee not found!\n");
        }
        // Change password
        else if(ch == 4) chg_pass(c, uid, ADM);
        // Logout
        else if(ch == 5) return;
    }
}

// SESSION HANDLER 
void* session_handler(void* arg) {
    int c = *(int*)arg; free(arg);
    char user[STR], pass[STR]; int role, uid;

    send_msg(c, "Username: "); get_input(c, user, STR);
    send_msg(c, "Password: "); get_input(c, pass, STR);

    if(!auth(user, pass, &role, &uid)) {
        send_msg(c, "Invalid login\n");
        close(c);
        return NULL;
    }

    pthread_mutex_lock(&sess_mutex);
    if(check_sess(user)) {
        pthread_mutex_unlock(&sess_mutex);
        send_msg(c, "User already logged in ! Log out from previous session to login again.\n");
        close(c);
        return NULL;
    }
    make_sess(user);
    pthread_mutex_unlock(&sess_mutex);

    char *user_heap = strdup(user);
    pthread_cleanup_push(clean_sess, user_heap);

    send_msg(c, "Logged In!\n");

    if(role == CUST) cust_menu(c, uid);
    else if(role == EMP) emp_menu(c, uid);
    else if(role == MGR) mgr_menu(c, uid);
    else if(role == ADM) adm_menu(c, uid);

    pthread_cleanup_pop(1);

    close(c);
    return NULL;
}