#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/sendfile.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>

#define RELEASE
#define FILE_RIGHT 0777
#define BUFF_SIZE (256*1024*1024)

char* folderPrefix = "./disk_";
char* metaFilePath = "./meta.txt"; // <file_origin_path, P, paddingZeroNum>
char* tmp_syndrome_file_path = "./syndrome"; // used for write

// used for a file read(some of them are alse used for write and repair)
char** origin_strip_names = NULL;
char* horizontal_strip_name = NULL;
char* diagonal_strip_name = NULL;

char* formula_5_tmp_S_cell_file_path = "./S_cell_5";
char* formula_7_tmp_S_cell_file_path = "./S_cell_7";
char** formula_8_tmp_S0s = NULL;
char** formula_9_tmp_S1s = NULL;

int lost_index_i = -1;
int lost_index_j = -1;
int lost_num = 0;

// used for repair
int number_erasures = 0;
int lost_disk_i = -1;
int lost_disk_2 = -1;

void usage(){
    printf("./evenodd write <file_name> <p>\n");
    printf("./evenodd read <file_name> <save_as>\n");
    printf("./evenodd repair <number_erasures> <idx0>\n");
}

long getFileSize(char* file_path){
    int fd = open(file_path, O_RDONLY);
    struct stat _stat;

    int ret = fstat(fd, &_stat);

    #ifndef RELEASE
    if(ret < 0){
        printf("getFileSize %s error, fd = %d\n", file_path, fd);
        printf("getFileSize fstat error, id %d, %s\n", errno, strerror(errno));
        exit(-1);
    }
    #endif RELEASE

    close(fd);
    return _stat.st_size; 
}

int getMeta(char* file_name, char* meta_buf){
    FILE *fp; 
    if((fp = fopen(metaFilePath,"r")) == NULL){
        printf("getMeta fopen error!\n");
        printf("error is %s\n", strerror(errno));
        exit(-1);
    }
    int index = 0;
    while (!feof(fp))
    {
        fgets(meta_buf, 1024, fp);  //读取一行
        int find = 1;
        for(int i=0; i<=strlen(file_name)-1; i++){
            if(meta_buf[i] != file_name[i]){
                find = 0;
                break;
            }    
        }
        if(find == 1){
            fclose(fp);
            return index;
        }
        index++;
    }
    fclose(fp);
    return -1;
}

int getMfromMeta(const char* meta_buf){
    int pos = 0;
    while(meta_buf[pos] != ' ')
        pos++;

    int ans = 0;
    for(int i=pos+1; meta_buf[i]!=' '; i++){
        ans += (meta_buf[i]-'0');
        ans *= 10;    
    }
    ans /= 10;
    return ans;
}

long getPadfromMeta(const char* meta_buf){
    int padding_zero_start_pos = 0;
    int space_num = 0;
    for(int i=0; i<=strlen(meta_buf)-1; i++){
        if(meta_buf[i] == ' ')
            space_num++;
        if(space_num == 2){
            padding_zero_start_pos = (i+1);
            break;
        }
    }

    int ans = 0;
    for(int i=padding_zero_start_pos; meta_buf[i]!='\n'; i++){
        ans += (meta_buf[i]-'0');
        ans *= 10;
    }
    ans /= 10;
    return ans;
}

int getPrimeFromMetaTxt(char* file_name){
    char read_buf[1024]={0}; 
    if(getMeta(file_name, read_buf) == -1){
        return -1;
    }

    return getMfromMeta(read_buf);
}

long getPaddingZeroFromMetaTxt(char* file_name){
    char read_buf[1024]={0}; 
    if(getMeta(file_name, read_buf) == -1){
        return -1;
    }

    return getPadfromMeta(read_buf);
}

int cellSumNum(int p){
    return p*(p-1);
}

long cellSize(char* file_name, int p){
    //这里的filename是用户写的文件名
    double cn = cellSumNum(p);
    double si = getFileSize(file_name);
    return ceil(si / cn);
}

long stripSize(char* file_name, int p){
    return (p-1) * cellSize(file_name, p);
}

int lastStripPaddingZeroNum(char* file_name, int p){
    long strip_size = stripSize(file_name, p);
    long file_size = getFileSize(file_name);
    if(file_size % strip_size == 0)
        return 0;
    return strip_size - (file_size % strip_size);
}   

void dataDiskFolderCheckAndMake(int p){
    if(p < 0){
        printf("invalid p %d\n", p);
        exit(-1);
    }
    char p_str[128]={0};
    char name_buf[128]={0};
    memcpy(name_buf, folderPrefix, strlen(folderPrefix));
    for(int i=0; i<=p-1+2; i++){
        memset(p_str, 0, 128);
        sprintf(p_str, "%d", i);
        memcpy(name_buf+strlen(folderPrefix), p_str, strlen(p_str));
        if((access(name_buf, F_OK)) == -1){
            if(mkdir(name_buf, FILE_RIGHT) < 0){
                printf("mkdir %s error, %s\n", name_buf, strerror(errno));
                exit(-1);
            }
        }
    }
}

void metaTxtCheckAndMake(){
    int fd = open(metaFilePath, O_RDWR | O_CREAT, FILE_RIGHT);
    close(fd);
}

void saveFileMeta(const char* meta_database_file_path, const char* file_path, int prime){
    int fd = open(meta_database_file_path, O_RDWR | O_APPEND);
    char prime_s[16] = {0};
    sprintf(prime_s, "%d", prime);

    char pad_s[16] = {0};
    sprintf(pad_s, "%d", lastStripPaddingZeroNum(file_path, prime));
    

    write(fd, file_path, strlen(file_path));
    write(fd, " ", 1);

    write(fd, prime_s, strlen(prime_s));
    write(fd, " ", 1);
    
    write(fd, pad_s, strlen(pad_s));
    write(fd, "\n", 1);
}

int getMetaNums(){
    if(getFileSize(metaFilePath) == 0)
        return 0;

    FILE *fp; 
    if((fp = fopen(metaFilePath,"r")) == NULL){
        printf("getMeta fopen error!\n");
        exit(-1);
    }

    char meta_buf[128]={0};
    int ans = 0;
    while (!feof(fp)){
        fgets(meta_buf, 1024, fp);
        ans++;
    }
    fclose(fp);
    // 减去一个空行
    return ans - 1;
}

void stripNameInit(int p, int file_id){
    char file_id_str[128]={0};
    sprintf(file_id_str, "%d", file_id);
    
    origin_strip_names=(char**)malloc(sizeof(char*) * p);
    horizontal_strip_name = (char*)malloc(sizeof(char) * 128);
    diagonal_strip_name = (char*)malloc(sizeof(char) * 128);
    char* strip_name = NULL;

    char p_str[128]={0};
    for(int i=0; i<=p+1; i++){
        if(i == p){
            strip_name = horizontal_strip_name;
        }else if(i == (p+1)){
            strip_name = diagonal_strip_name;
        }else{
            origin_strip_names[i] = (char*)malloc(sizeof(char) * 128);
            strip_name = origin_strip_names[i];
        }
        sprintf(p_str, "%d", i);
        memset(strip_name, 0, 128);
        memcpy(strip_name, folderPrefix, strlen(folderPrefix));
        memcpy(strip_name+strlen(folderPrefix), p_str, strlen(p_str));
        memcpy(strip_name+strlen(folderPrefix)+strlen(p_str), "/", 1);
        memcpy(strip_name+strlen(folderPrefix)+strlen(p_str)+1, file_id_str, strlen(file_id_str));
    }
}

void writeOriginStrip(char* file_path, int p){
    //数据列
    int fd_source = open(file_path, O_RDONLY);
    long strip_size = stripSize(file_path, p);
    
    for(int i=0; i<=p-1; i++){
        int fd_strip = open(origin_strip_names[i], O_RDWR | O_CREAT, FILE_RIGHT);
        if(ftruncate(fd_strip, strip_size) < 0){
            printf("truncate error, %d, %s, errno %d\n", i, strerror(errno), errno);
            exit(-1);
        }

        lseek(fd_strip, 0, SEEK_SET);

        long rest_size = strip_size;
        while(rest_size > 0){
            long send_size = rest_size < BUFF_SIZE ? rest_size : BUFF_SIZE;
            if(sendfile(fd_strip, fd_source, NULL, send_size) == -1){
                printf("sendfile error, %d, %s, errno %d\n", i, strerror(errno), errno);
                exit(-1);
            }
            rest_size -= send_size;
        }
        close(fd_strip);
    }
}

void XOR(unsigned char* buf1, unsigned char*buf2, long len){
    for(long i=0; i<=len-1; i++)
        buf1[i] = buf1[i]^buf2[i]; 
}

void writeRedundancyFileHorizontal(int p){
    unsigned char* write_buf=(unsigned char*)malloc(BUFF_SIZE);
    memset(write_buf, 0, BUFF_SIZE);
    unsigned char* read_buf=(unsigned char*)malloc(BUFF_SIZE);
    memset(read_buf, 0, BUFF_SIZE);
    
    int* fds=(int*)malloc(p*sizeof(int));
    for(int i=0; i<=p-1; i++)
        fds[i] = open(origin_strip_names[i], O_RDONLY);
    
    int hangFd = open(horizontal_strip_name, O_WRONLY | O_CREAT | O_APPEND, FILE_RIGHT);
    const long strip_size = getFileSize(origin_strip_names[0]);
    const long cell_size = strip_size / (p-1);
    for(int i=0; i<=p-2; i++)
        //XORcell会改变文件的打开指针，不用seek，一个cell接着一个顺着往下XOR就行
        XORcell(hangFd, fds, p, cell_size);
    
    // 关闭文件、释放内存
    for(int i=0; i<=p-1; i++)
        close(fds[i]);
    close(hangFd);
    free(fds);
    free(write_buf);
    free(read_buf);
}

void writeSyndromeCellFile(long cell_size, int p){
    int fd_syn = open(tmp_syndrome_file_path, O_RDWR | O_APPEND | O_CREAT, FILE_RIGHT);
    unsigned char* write_buf = (unsigned char*)malloc(BUFF_SIZE);
    memset(write_buf, 0, BUFF_SIZE);
    unsigned char* read_buf = (unsigned char*)malloc(BUFF_SIZE);
    memset(read_buf, 0, BUFF_SIZE);
    
    int* fds=(int*)malloc(p*sizeof(int));
    
    for(int col=1; col<=p-1; col++){
        int row = p-col-1;
        fds[col] = open(origin_strip_names[col], O_RDONLY);
        lseek(fds[col], row*cell_size, SEEK_SET);    
    }

    long rest_size = cell_size;
    while(rest_size > 0){
        int read_size = 0;
        memset(write_buf, 0, BUFF_SIZE);
        for(int i=1; i<=p-1; i++){
            read_size = rest_size<BUFF_SIZE?rest_size:BUFF_SIZE;
            read(fds[i], read_buf, read_size);
            XOR(write_buf, read_buf, read_size);
        }
        rest_size -= read_size;
        write(fd_syn, write_buf, read_size);
    }

    off_t syndrome_size;
    syndrome_size = lseek(fd_syn, 0, SEEK_END);
    if(syndrome_size != cell_size){
        printf("syndrome cell file generate error, size not valid\n"); 
        exit(-1);
    }

    for(int i=0; i<=p-1; i++)
        close(fds[i]);
    close(fd_syn);
    free(fds);
    free(write_buf);
    free(read_buf);
}

void removeTmpFile(const char* s){
    if(remove(s) < 0){
        printf("remove syn fail\n");
        exit(-1);
    }
}

void writeRedundancyFileDiagonal(long cell_size, int m){
    int synFd = open(tmp_syndrome_file_path, O_RDONLY, FILE_RIGHT);
    int duiFd = open(diagonal_strip_name, O_WRONLY | O_CREAT | O_APPEND, FILE_RIGHT);

    int* fds=(int*)malloc(m*sizeof(int));
    for(int i=0; i<=m-1; i++)
        fds[i] = open(origin_strip_names[i], O_RDONLY);
    
    unsigned char* write_buf = (unsigned char*)malloc(BUFF_SIZE);
    unsigned char* read_buf = (unsigned char*)malloc(BUFF_SIZE);
    for(int l=0; l<=m-2; l++){
        // 数据列推到指定格子
        for(int t=0; t<=m-1; t++){
            int pos = notation(l-t, m);
            lseek(fds[t], pos*cell_size, SEEK_SET);
        }
            
        // syndrome文件offset回到起点
        lseek(synFd, 0, SEEK_SET);

        long rest_size = cell_size;
        while(rest_size > 0){
            int read_size = rest_size<BUFF_SIZE?rest_size:BUFF_SIZE;
            memset(write_buf, 0, BUFF_SIZE);

            // 数据列异或
            for(int t=0; t<=m-1; t++){
                // 如果这个数据列被推到了m-1行，那么就不异或
                if(notation(l-t, m) != (m-1)){
                    read(fds[t], read_buf, read_size);
                    XOR(write_buf, read_buf, read_size);
                } 
            }

            // syndrom异或
            read(synFd, read_buf, read_size);
            XOR(write_buf, read_buf, read_size);

            // write
            write(duiFd, write_buf, read_size);

            rest_size -= read_size;
        }
    }

    close(duiFd);
    close(synFd);
    for(int i=0; i<=m-1; i++)   
        close(fds[i]);
    
    free(write_buf);
    free(read_buf);
    free(fds);
}

typedef struct horARG{
    int p;
}horARG;

typedef struct diaARG{
    char* file_path;
    int p;
}diaARG;

void thread_wirte_horizontal(void* args){
    horARG* r = (horARG*)args;
    writeRedundancyFileHorizontal(r->p);
}

void thread_write_diagonal(void* args){
    diaARG* r = (diaARG*)args;
    long cell_size = cellSize(r->file_path, r->p);
    writeSyndromeCellFile(cell_size, r->p);
    writeRedundancyFileDiagonal(cell_size, r->p);
    removeTmpFile(tmp_syndrome_file_path);//使用全局变量
}

void evenoddWrite(char* file_path, int p){
    dataDiskFolderCheckAndMake(p);
    stripNameInit(p, getMetaNums());
    saveFileMeta(metaFilePath, file_path, p);
    
    writeOriginStrip(file_path, p);
    
    // 针对数据列只有并发读。
    pthread_t t_hor;
    horARG args1;
    args1.p = p;
    pthread_create(&t_hor, NULL, thread_wirte_horizontal, &args1);

    pthread_t t_dia;
    diaARG args2;
    args2.p = p;
    args2.file_path = file_path;
    pthread_create(&t_dia, NULL, thread_write_diagonal, &args2);

    void* thread_result;
    pthread_join(t_hor, &thread_result);
    pthread_join(t_dia, &thread_result);
}

/*
    丢失总结：
        1) 一个数据列
        2) 一个行校验列
        3) 一个对角线(Diagonal)校验列

        4) 两个数据列
        5) 一个数据列，一个行校验列
        6) 一个数据列，一个对角线(Diagonal)校验列
        7) 两个校验列

        8) 三个列及以上，不可恢复
*/
int existFile(const char* file_name){
    char meta_buf[128] = {0};
    if(getMeta(file_name, meta_buf) == -1)
        return false;
    return true;
}

void lostScan(const char* file_name){  
    int p = getPrimeFromMetaTxt(file_name);
    lostScanM(p);
}

void lostScanM(int m){
    lost_num = 0;
    for(int i=0; i<=m-1; i++)
        //如果访问不到这个列，说明，要么列文件丢了，要么整个disk目录丢了
        if(access(origin_strip_names[i], F_OK)  < 0){
            lost_num ++ ;
            if(lost_index_i == -1)
                lost_index_i = i;
            else
                lost_index_j = i;
        }
    
    if(access(horizontal_strip_name, F_OK)  < 0){
        lost_num ++ ;
        if(lost_index_i == -1)
                lost_index_i = m;
            else
                lost_index_j = m;
    }
        
    if(access(diagonal_strip_name, F_OK)  < 0){
        lost_num ++ ;
        if(lost_index_i == -1)
                lost_index_i = m+1;
            else
                lost_index_j = m+1;
    }
}

void mergeDataStrip(const char** data_strip_names, int m, const char* save_as, int padding_zero){
    int fd_save = open(save_as, O_WRONLY | O_CREAT, FILE_RIGHT);
    long strip_size = getFileSize(data_strip_names[0]);
    if(ftruncate(fd_save, strip_size*m) < 0){
        printf("mergeDataStrip ftruncate1 error, %s\n", strerror(errno));
        exit(-1);
    }
    lseek(fd_save, 0, SEEK_SET);

    for(int i=0; i<=m-1; i++){
        int fd_strip = open(data_strip_names[i], O_RDONLY, FILE_RIGHT);
        long rest_size = strip_size;
        while(rest_size > 0){
            long send_size = rest_size < BUFF_SIZE ? rest_size : BUFF_SIZE;
            if(sendfile(fd_save, fd_strip, NULL, send_size) == -1){
                printf("mergeDataStrip sendfile %d error, %s\n", i, strerror(errno));
                exit(-1);
            }
            rest_size -= send_size;
        }
    }

    long new_size = strip_size * m - padding_zero;
    if(ftruncate(fd_save, new_size) < 0){
        printf("mergeDataStrip ftruncate2 error, %s\n", strerror(errno));
        exit(-1);
    }
}

void mergeDataStrip_destination(const char** data_strip_names, 
                                int m, 
                                int fd_save, 
                                int lost_index,
                                long strip_size){

    for(int i=0; i<=m-1; i++){
        if(i != lost_index){
            int fd_strip = open(data_strip_names[i], O_RDONLY, FILE_RIGHT);
            lseek(fd_save, i*strip_size, SEEK_SET);
            long rest_size = strip_size;
            while(rest_size > 0){
                long send_size = rest_size < BUFF_SIZE ? rest_size : BUFF_SIZE;
                if(sendfile(fd_save, fd_strip, NULL, send_size) == -1){
                    printf("mergeDataStrip sendfile %d error, %s\n", i, strerror(errno));
                    exit(-1);
                }
                rest_size -= send_size;
            }
        }
    }
}

void restoreOneLostDataStrip(const char* strip_name, int m, int lost_index){
    int fd_restore_strip = open(strip_name, O_WRONLY | O_CREAT | O_APPEND, FILE_RIGHT);
    int* fds = (int*)calloc(m, sizeof(int));
    for(int i=0; i<=m; i++){
        if(i<m && i!=lost_index){//data strip
            fds[i] = open(origin_strip_names[i], O_RDONLY);
        }else if(i==m){//horizon parity strip
            fds[i] = open(horizontal_strip_name, O_RDONLY);
        }
    }

    unsigned char* write_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    unsigned char* read_buf = (unsigned char*)calloc(1, BUFF_SIZE);

    long rest_size = getFileSize(horizontal_strip_name);
    while(rest_size > 0){
        memset(write_buf, 0, BUFF_SIZE);//清除上次的残留
        int read_size = BUFF_SIZE < rest_size ? BUFF_SIZE : rest_size;
        for(int i=0; i<=m; i++){
            if(i != lost_index){
                read(fds[i], read_buf, read_size);
                XOR(write_buf, read_buf, read_size);
            }
        }

        write(fd_restore_strip, write_buf, read_size);
        rest_size -= read_size;
    } 

    for(int i=0; i<=m; i++)
        if(i != lost_index)
            close(fds[i]);
    free(write_buf);
    free(read_buf);
}

void restoreOneLostDataStrip_destination(int m, int lost_index, 
                            int fd_save,
                            long restore_offest,
                            long strip_size){

    lseek(fd_save, restore_offest, SEEK_SET);
        
    int* fds = (int*)calloc(m, sizeof(int));
    for(int i=0; i<=m; i++){
        if(i<m && i!=lost_index){//data strip
            fds[i] = open(origin_strip_names[i], O_RDONLY);
        }else if(i==m){//horizon parity strip
            fds[i] = open(horizontal_strip_name, O_RDONLY);
        }
    }

    unsigned char* write_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    unsigned char* read_buf = (unsigned char*)calloc(1, BUFF_SIZE);

    long rest_size = strip_size;
    while(rest_size > 0){
        memset(write_buf, 0, BUFF_SIZE);//清除上次的残留
        int read_size = BUFF_SIZE < rest_size ? BUFF_SIZE : rest_size;
        for(int i=0; i<=m; i++){
            if(i != lost_index){
                read(fds[i], read_buf, read_size);
                XOR(write_buf, read_buf, read_size);
            }
        }

        write(fd_save, write_buf, read_size);
        rest_size -= read_size;
    } 

    for(int i=0; i<=m; i++)
        if(i != lost_index)
            close(fds[i]);
    free(write_buf);
    free(read_buf);
}

void readLostDataStrip_Situation(int m, const char* save_as, int padding_zero){
    // 1.先准备好要恢复的文件
    int fd_save = open(save_as, O_RDWR | O_CREAT, FILE_RIGHT);
    long strip_size = getFileSize(horizontal_strip_name);
    if(ftruncate(fd_save, strip_size * m) < 0){
        printf("mergeDataStrip ftruncate1 error, %s\n", strerror(errno));
        exit(-1);
    }

    /*
        2.利用未丢失的数据列和行校验列恢复丢失的数据列，但是恢复数据直接写入目标文件
          写入的位置和丢失的数据列编号有关，假如m=3，而丢失了第0列数据，
          那么恢复的数据需要写入目标文件（目标文件有m=3个块）的开头的第一个块。
    */
    long restore_offest = lost_index_i * strip_size;
    restoreOneLostDataStrip_destination(m, lost_index_i,
                                fd_save,
                                restore_offest,
                                strip_size);

    // 3.把其余未丢失的数据列输入目标文件
    mergeDataStrip_destination(origin_strip_names, m, fd_save, lost_index_i, strip_size);

    // 4.截掉末尾多余的0
    long new_size = strip_size * m - padding_zero;
    if(ftruncate(fd_save, new_size) < 0){
        printf("mergeDataStrip ftruncate2 error, %s\n", strerror(errno));
        exit(-1);
    }
}

int notation(int n, int m){
    if((n%m) >= 0)
        return n%m;
    return (n%m) + m;
}

void formula_5(int m, int lost_index){
    // 1.准备文件
    int fd_tmp_s = open(formula_5_tmp_S_cell_file_path, 
                        O_WRONLY | O_CREAT | O_APPEND, FILE_RIGHT);

    int* fds = (int*)calloc(m+2, sizeof(int));
    for(int i=0; i<=m+1; i++){
        if(i<m && i!=lost_index){
            fds[i] = open(origin_strip_names[i], O_RDONLY);
        }else if(i==m){
            //horizon has been lost, do not care.
            ;
        }else if(i==m+1){
            fds[i] = open(diagonal_strip_name, O_RDONLY); 
        }
    }

    // 2.把起始offset准备好
    const long strip_size = getFileSize(diagonal_strip_name);
    const long cell_size = strip_size / (m-1);
    for(int l=0; l<=m+1; l++){
        if(l<m && l!=lost_index){
            lseek(fds[l], cell_size * notation(lost_index-l-1, m), SEEK_SET);
        }else if(l==m){
            ;
        }else if(l==m+1){
            lseek(fds[l], cell_size * notation(lost_index-1, m), SEEK_SET);
        }
    }

    // 3.XOR write
    unsigned char* write_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    unsigned char* read_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    long rest_size = cell_size;
    while(rest_size > 0){
        int read_size = BUFF_SIZE < rest_size ? BUFF_SIZE : rest_size;
        memset(write_buf, 0, BUFF_SIZE);

        for(int l=0; l<=m+1; l++){
            if(l == lost_index || l == m)
                continue;

            if(l==(m+1) && notation(lost_index-1, m) == (m-1))
                continue;
            
            if(l<=(m-1) && notation(lost_index-l-1, m) == (m-1))
                continue;

            read(fds[l], read_buf, read_size);
            XOR(write_buf, read_buf, read_size); 
        }

        write(fd_tmp_s, write_buf, read_size);
        rest_size -= read_size;
    }

    // 4.关闭文件
    close(fd_tmp_s);
    for(int i=0; i<=m+1; i++)
        if(i<m && i!=lost_index)
            close(fds[i]);
        else if(i==m+1)
            close(fds[i]); 
    free(write_buf);
    free(read_buf);
}

void formula_6(int m, int lost_index){
    // <formula5 S文件> + <对角线校验列> + <未丢失的数据列> --> <丢失的数据列>

    // 1.准备文件
    int fd_restore_strip = open(origin_strip_names[lost_index], 
        O_WRONLY | O_CREAT | O_APPEND, FILE_RIGHT);

    int fd_sfile = open(formula_5_tmp_S_cell_file_path, O_RDONLY);

    int* fds = (int*)calloc(m+2, sizeof(int));
    for(int i=0; i<=m+1; i++){
        if(i<m && i!=lost_index){
            fds[i] = open(origin_strip_names[i], O_RDONLY);
        }else if(i==m){
            //horizon has been lost, do not care.
            ;
        }else if(i==m+1){
            fds[i] = open(diagonal_strip_name, O_RDONLY); 
        }
    }

    // 2.XOR write
    unsigned char* write_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    unsigned char* read_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    const long strip_size = getFileSize(diagonal_strip_name);
    const long cell_size = strip_size / (m-1);
    for(int k=0; k<=m-2; k++){
        // 设置读取位置的起点，也就是哪个格子
        lseek(fd_sfile, 0, SEEK_SET);
        lseek(fds[m+1], notation(k + lost_index, m) * cell_size, SEEK_SET);
        for(int l=0; l<=m-1; l++)
            if(l != lost_index)
                lseek(fds[l], notation(k + lost_index - l, m) * cell_size, SEEK_SET);
            
        //开始读每个格子，然后异或，然后写，S文件自己就是一个格子
        long rest_size = cell_size;
        while(rest_size > 0){
            memset(write_buf, 0, BUFF_SIZE);
            int read_size = BUFF_SIZE < rest_size ? BUFF_SIZE : rest_size;

            read(fd_sfile, read_buf, read_size);
            XOR(write_buf, read_buf, read_size);

            for(int l=0; l<=m+1; l++){
                if(l == lost_index || l == m)
                    continue;
                if(l==(m+1) && notation(k + lost_index, m)==(m-1))
                    continue;
                if(l<=(m-1) && notation(k + lost_index - l, m)==(m-1))
                    continue;
                // 对角线校验列和未丢失的数据列
                read(fds[l], read_buf, read_size);
                XOR(write_buf, read_buf, read_size); 
            }

            //写
            write(fd_restore_strip, write_buf, read_size);
            rest_size -= read_size;
        }
    }

    // 3.关闭文件
    close(fd_sfile);
    for(int i=0; i<=m+1; i++)
        if(i<m && i!=lost_index)
            close(fds[i]);
        else if(i==m+1)
            close(fds[i]);
    free(write_buf);
    free(read_buf);
}

void readLostDataAndHorizon_Situation(int m, const char* save_as, int padding_zero){
    formula_5(m, lost_index_i);
    formula_6(m, lost_index_i);
    mergeDataStrip(origin_strip_names, m, save_as, padding_zero);
    
    //题目没要求恢复数据列，这里删掉
    // 公式5产生的S也是额外的，也删掉
    removeTmpFile(origin_strip_names[lost_index_i]);
    removeTmpFile(formula_5_tmp_S_cell_file_path);
}

void formula_7(int m){
    // 1.准备文件
    int fd_tmp_s = open(formula_7_tmp_S_cell_file_path, 
                        O_WRONLY | O_CREAT | O_APPEND, FILE_RIGHT);

    int fd_horizontal = open(horizontal_strip_name, O_RDONLY);
    int fd_diagonal = open(diagonal_strip_name, O_RDONLY);

    // 2.XOR write
    unsigned char* write_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    unsigned char* read_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    const long strip_size = getFileSize(diagonal_strip_name);
    const long cell_size = strip_size / (m-1);

    long rest_size = cell_size;
    long accumulate_size = 0;
    while(rest_size > 0){
        int read_size = BUFF_SIZE < rest_size ? BUFF_SIZE : rest_size;
        memset(write_buf, 0, BUFF_SIZE);

        // 行校验列异或
        for(int l=0; l<=m-2; l++){
            lseek(fd_horizontal, l*cell_size + accumulate_size, SEEK_SET);
            read(fd_horizontal, read_buf, read_size);
            XOR(write_buf, read_buf, read_size);
        }
        
        // 对角线校验列异或
        for(int l=0; l<=m-2; l++){
            lseek(fd_diagonal, l*cell_size + accumulate_size, SEEK_SET);
            read(fd_diagonal, read_buf, read_size);
            XOR(write_buf, read_buf, read_size);
        }

        //写
        write(fd_tmp_s, write_buf, read_size);

        rest_size -= read_size;
        accumulate_size += read_size;
    }
    
    // 3.关闭文件释放空间
    close(fd_diagonal);
    close(fd_horizontal);
    close(fd_tmp_s);
    free(write_buf);
    free(read_buf);
}

void formula_8(int m, int lost_i, int lost_j, 
                const char** data_names, 
                const char* horizontal_name,
                const char** horizontal_syndrome_names){

    // 1.准备文件
    // 未丢失的数据列和行校验列
    // 这里有两个fd是空的，因为丢了
    int* fds = (int*)calloc(m+1, sizeof(int));
    for(int i=0; i<=m; i++){
        if(i<m && i!=lost_i && i!=lost_j){
            fds[i] = open(data_names[i], O_RDONLY);
        }else if(i==m){
            fds[i] = open(horizontal_name, O_RDONLY); 
        }
    }
    // 行syndrome文件
    int* hs_fds = (int*)calloc(m, sizeof(int));
    for(int u=0; u<=m-1; u++){
        hs_fds[u]= open(horizontal_syndrome_names[u], O_WRONLY | O_CREAT | O_APPEND, FILE_RIGHT);
        lseek(hs_fds[u], 0, SEEK_SET);
    }
    
    // 2.产生m个horizontal syndrome文件，每个文件都是一个cell的大小
    unsigned char* write_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    unsigned char* read_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    const long strip_size = getFileSize(diagonal_strip_name);
    const long cell_size = strip_size / (m-1);
    // 数据列和行校验列就不用seek了，因为是顺着往下read
    for(int u=0; u<=m-1; u++){
        if(u == m-1){
            ftruncate(hs_fds[u], cell_size);
            break;
        }

        long rest_size = cell_size;
        while(rest_size > 0){
            int read_size = BUFF_SIZE < rest_size ? BUFF_SIZE : rest_size;
            memset(write_buf, 0, BUFF_SIZE);
            for(int l=0; l<=m; l++){
                if(l!=lost_i && l!=lost_j){
                    read(fds[l], read_buf, read_size);
                    XOR(write_buf, read_buf, read_size);
                }
            }
            
            //写
            write(hs_fds[u], write_buf, read_size);
            rest_size -= read_size;
        }
    }

    // 3.关闭文件 释放空间
    for(int u=0; u<=m-1; u++)
        close(hs_fds[u]);
    for(int i=0; i<=m; i++)
        if(i!=lost_i && i!=lost_j)
            close(fds[i]);
    free(write_buf);
    free(read_buf);
}

void formula_9(int m, int lost_i, int lost_j, 
                const char** diagonal_syndrome_names,
                const char* formula_7_s_name,
                const char* diagonal_name,
                const char** data_names){

    // 1.准备文件
    // 目标：对角线syndrome文件
    int* ds_fds = (int*)calloc(m, sizeof(int));
    for(int u=0; u<=m-1; u++){
        ds_fds[u]= open(diagonal_syndrome_names[u], O_WRONLY | O_CREAT | O_APPEND, FILE_RIGHT);
        lseek(ds_fds[u], 0, SEEK_SET);
    }
    // 源：<formula7产生的S> <对角线校验列> <未丢失的数据列>
    int fd_s_7 = open(formula_7_s_name, O_RDONLY);
    int fd_diagonal = open(diagonal_name, O_RDONLY);

    int* fds = (int*)calloc(m, sizeof(int));
    for(int i=0; i<=m-1; i++){
        if(i!=lost_i && i!=lost_j)
            fds[i] = open(data_names[i], O_RDONLY);
    }

    // 2.XOR write
    unsigned char* write_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    unsigned char* read_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    const long strip_size = getFileSize(diagonal_strip_name);
    const long cell_size = strip_size / (m-1);
    for(int u=0; u<=m-1; u++){
        // S文件推到起点
        lseek(fd_s_7, 0, SEEK_SET);
        // 对角线校验列推到第u个格子
        if(u != m-1)
            lseek(fd_diagonal, u*cell_size, SEEK_SET);

        // 推未丢失的数据列
        for(int l=0; l<=m-1; l++){
            if(l!=lost_i && l!=lost_j){
                if(notation(u-l, m) == (m-1)){
                    continue;
                    //反正也不读这个列了，不seek也行
                }
                lseek(fds[l], notation(u-l, m)*cell_size, SEEK_SET);
            }
        }
        
        // 扫描一个格子的数据(cell_size)
        long rest_size = cell_size;
        while(rest_size > 0){
            int read_size = BUFF_SIZE < rest_size ? BUFF_SIZE : rest_size;
            memset(write_buf, 0, BUFF_SIZE);

            // S文件异或
            read(fd_s_7, read_buf, read_size);
            XOR(write_buf, read_buf, read_size);

            if(u!=m-1){
                // 对角线校验列异或
                read(fd_diagonal, read_buf, read_size);
                XOR(write_buf, read_buf, read_size);
            }
            
            // 未丢失的数据列异或
            for(int l=0; l<=m-1; l++){
                if(l!=lost_i && l!=lost_j){
                    if(notation(u-l, m) != m-1){
                        read(fds[l], read_buf, read_size);
                        XOR(write_buf, read_buf, read_size);
                    }
                }
            }

            //写
            write(ds_fds[u], write_buf, read_size);
            
            rest_size -= read_size;
        }
    }

    // 3.关闭文件、释放空间 
    for(int u=0; u<=m-1; u++)
        close(ds_fds[u]);
    close(fd_s_7); 
    close(fd_diagonal);         
    for(int i=0; i<=m-1; i++)
        if(i!=lost_i && i!=lost_j)
            close(fds[i]);
    free(write_buf);
    free(read_buf);
}

void XORcell(int dest_fd, int* src_fds, int src_num, long cell_size){
    /*
        调用这个函数时，文件指针都准备好了
        文件要么是一个单独的S文件（一个格子大小）
        要么时一个列文件（已经被推到指定格子去了）
        从文件当前打开的指针开始操作
        XOR一个cell的大小
        往目标fd写一个cell的大小
        -- 该函数会改变文件打开的指针，不置回原位 --
    */
    unsigned char* write_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    unsigned char* read_buf = (unsigned char*)calloc(1, BUFF_SIZE);
    long rest_size = cell_size;
    while(rest_size > 0){
        int read_size = BUFF_SIZE < rest_size ? BUFF_SIZE : rest_size;
        memset(write_buf, 0, BUFF_SIZE);//清除上次的残留

        for(int l=0; l<=src_num-1; l++){
            read(src_fds[l], read_buf, read_size);
            XOR(write_buf, read_buf, read_size);
        }
        write(dest_fd, write_buf, read_size);
        rest_size -= read_size;
    }

    free(write_buf);
    free(read_buf);
}

void recursive123(int m, int lost_i, int lost_j,
                    const char* i_name,
                    const char* j_name,
                    const char** horizontal_syndrome_names,
                    const char** diagonal_syndrome_names){
    // 1.准备文件
    // 目标：i和j 两个丢失的数据列 不能使用追加写，因为s决定写哪个格子
    // 先把目标列拉长用0填充，然后再写
    const long strip_size = getFileSize(diagonal_strip_name);
    const long cell_size = strip_size / (m-1);

    int fd_i = open(i_name, O_CREAT | O_RDWR, FILE_RIGHT);
    ftruncate(fd_i, strip_size);
        
    int fd_j = open(j_name, O_CREAT | O_RDWR, FILE_RIGHT);
    ftruncate(fd_j, strip_size);

    // 源：formula 9 产生的m个行syndrome和m个对角线syndrome
    int* hs_fds = (int*)calloc(m, sizeof(int));
    int* ds_fds = (int*)calloc(m, sizeof(int));
    for(int u=0; u<=m-1; u++){
        hs_fds[u] = open(horizontal_syndrome_names[u], O_RDONLY);
        ds_fds[u] = open(diagonal_syndrome_names[u], O_RDONLY);
    }

    // 2.XOR write
    int s = notation(lost_i - lost_j - 1, m);
    int fds[2] = {0};
    while(s != m-1){
        // 计算J列  
        lseek(fd_j, s*cell_size, SEEK_SET);
        int S1 = ds_fds[notation(lost_j + s, m)];
        lseek(S1, 0, SEEK_SET);

        if(notation(s + lost_j - lost_i, m) == m-1){
            fds[0] = S1;
            XORcell(fd_j, fds, 1, cell_size);
        }else{
            fds[0] = S1;
            fds[1] = fd_i;
            lseek(fd_i, notation(s + lost_j - lost_i, m)*cell_size, SEEK_SET);
            XORcell(fd_j, fds, 2, cell_size);
        }
        
        // 计算I列
        lseek(fd_i, s*cell_size, SEEK_SET);
        lseek(fd_j, s*cell_size, SEEK_SET);
        int S0 = hs_fds[s];
        lseek(S0, 0, SEEK_SET);
        fds[0] = S0;
        fds[1] = fd_j;
        XORcell(fd_i, fds, 2, cell_size);
        
        s = notation(s - lost_j + lost_i, m);
    }

    // 3.关闭文件/释放空间
    close(fd_i);
    close(fd_j);
    for(int u=0; u<=m-1; u++){
        close(hs_fds[u]);
        close(ds_fds[u]);
    }
    free(hs_fds);
    free(ds_fds);
}

void formula_8_tmp_S0_name_init(int m){
    formula_8_tmp_S0s = (char**)malloc(sizeof(char*) * m);
    for(int i=0; i<=m-1; i++){
        formula_8_tmp_S0s[i] = (char*)malloc(128);
        sprintf(formula_8_tmp_S0s[i], "formula_8_tmp_S0_%d", i);
    }
}

void formula_8_tmp_S0_name_free(int m){
    for(int i=0; i<=m-1; i++)
        free(formula_8_tmp_S0s[i]);
    free(formula_8_tmp_S0s);
}

void formula_9_tmp_S1_name_init(int m){
    formula_9_tmp_S1s = (char**)malloc(sizeof(char*) * m);
    for(int i=0; i<=m-1; i++){
        formula_9_tmp_S1s[i] = (char*)malloc(128);
        sprintf(formula_9_tmp_S1s[i], "formula_9_tmp_S1_%d", i);
    }
}

void formula_9_tmp_S1_name_free(int m){
    for(int i=0; i<=m-1; i++)
        free(formula_9_tmp_S1s[i]);
    free(formula_9_tmp_S1s);
}

void repairTwoLostStrip(int, int);
void readLostTwoData_Situation(int m, const char* save_as, int padding_zero){
    repairTwoLostStrip(m, padding_zero);

    mergeDataStrip(origin_strip_names, m, save_as, padding_zero);

    removeTmpFile(origin_strip_names[lost_index_i]);
    removeTmpFile(origin_strip_names[lost_index_j]);
}

void evenoddRead(const char* file_name, const char* save_as){
    time_t seconds_start;
    seconds_start = time(NULL);

    if(!existFile(file_name)){
        printf("File does not exist!\n");
        exit(-1);
    }

    int padding_zero = getPaddingZeroFromMetaTxt(file_name);
    char* meta_buf = (char*)malloc(sizeof(char) * 256);
    int file_id = getMeta(file_name, meta_buf);
    int m = getPrimeFromMetaTxt(file_name);

    //这里初始化的名字是想象中的，这个文件不一定真的存在，有可能被删了。
    stripNameInit(m, file_id);

    //有些disk文件夹被毁掉了，这里先准备好，防止等会儿open create一个文件失败
    dataDiskFolderCheckAndMake(m);

    lostScan(file_name);
    if(lost_num > 2){
        printf("File corrupted!\n");
        exit(-1);
    }
    if(lost_num == 1){
        if(lost_index_i <= m-1){
            #ifndef RELEASE 
            printf("丢失一个数据列 %d.\n", lost_index_i); 
            #endif
            readLostDataStrip_Situation(m, save_as, padding_zero);
        }
        else if(lost_index_i == m){
            #ifndef RELEASE
            printf("丢失一个行校验列 %d.\n", lost_index_i);
            #endif
            mergeDataStrip(origin_strip_names, m, save_as, padding_zero);
        }
        else if(lost_index_i == m+1){
            #ifndef RELEASE
            printf("丢失一个对角线列 %d.\n", lost_index_i);
            #endif
            mergeDataStrip(origin_strip_names, m, save_as, padding_zero);
        }
    }else if(lost_num == 2){
        if(lost_index_i == m && lost_index_j == m+1){
            #ifndef RELEASE
            printf("丢失两个校验列\n");
            #endif
            mergeDataStrip(origin_strip_names, m, save_as, padding_zero);
        }
        else if(lost_index_i < m && lost_index_j == m){
            #ifndef RELEASE
            printf("丢失一个数据列%d 和 一个行校验列. 直接用对角线校验列恢复数据列，不用管行校验列\n", lost_index_i);
            #endif
            readLostDataAndHorizon_Situation(m, save_as, padding_zero);
        }
        else if(lost_index_i < m && lost_index_j == m+1){
            #ifndef RELEASE
            printf("丢失一个数据列, 一个对角线校验列. 相当于丢失一个数据列，用行校验列恢复就行.\n");
            #endif
            readLostDataStrip_Situation(m, save_as, padding_zero);
        }
        else if(lost_index_i < m && lost_index_j < m){
            #ifndef RELEASE
            printf("丢失两个数据列 %d, %d.\n", lost_index_i, lost_index_j);
            #endif
            readLostTwoData_Situation(m, save_as, padding_zero);
        }
        else{
            #ifndef RELEASE
            printf("奇怪的错误 i=%d, j=%d\n", lost_index_i, lost_index_j);
            #endif
            exit(-1);  
        }
    }else if(lost_num == 0){
        #ifndef RELEASE
        printf("未丢失数据列.\n");
        #endif
        mergeDataStrip(origin_strip_names, m, save_as, padding_zero);
    }else{
        printf("impossible...\n");
        exit(-1); 
    }
    // file name是存的时候的名字
    // save as是读的时候另存为的名字，这里另存为的地方应该是和evenodd同目录的地方
    time_t seconds_end;
    seconds_end = time(NULL);
    #ifndef RELEASE
    printf("本次读操作消耗 = %ld秒\n", seconds_end - seconds_start);
    #endif
}

// ----- ----- ----- REPAIR ----- ----- -----
void freeStripNames(int m){
    if(origin_strip_names != NULL){
        for(int i=0; i<=m-1; i++)
            free(origin_strip_names[i]);
    }
    free(horizontal_strip_name);
    free(diagonal_strip_name);
}

typedef struct formula_8_ARG{
    int m;
    int lost_i;
    int lost_j;
    const char** data_names;
    const char* horizontal_name;
    const char** horizontal_syndrome_names;
}formula_8_ARG;

typedef struct formula_9_ARG{
    int m; 
    int lost_i; 
    int lost_j;
    const char** diagonal_syndrome_names;
    const char* formula_7_s_name;
    const char* diagonal_name;
    const char** data_names;
}formula_9_ARG;

void thread_formula_8(void* args){
    formula_8_ARG* r = (formula_8_ARG*)args;
    formula_8(r->m ,r->lost_i, r->lost_j, 
                                r->data_names,
                                r->horizontal_name,
                                r->horizontal_syndrome_names);
}

void thread_formula_9(void* args){
    formula_9_ARG* r = (formula_9_ARG*)args;
    formula_9(r->m, r->lost_i, r->lost_j,
                            r->diagonal_syndrome_names,
                            r->formula_7_s_name,
                            r->diagonal_name,
                            r->data_names);
}

void repairTwoLostStrip(int m, int padding_zero){
    formula_7(m);   

    formula_8_tmp_S0_name_init(m);
    pthread_t t_8;
    formula_8_ARG args8;
    args8.m = m;
    args8.lost_i = lost_index_i;
    args8.lost_j = lost_index_j;
    args8.data_names = origin_strip_names;
    args8.horizontal_name = horizontal_strip_name;
    args8.horizontal_syndrome_names = formula_8_tmp_S0s;
    pthread_create(&t_8, NULL, thread_formula_8, &args8);


    formula_9_tmp_S1_name_init(m);
    pthread_t t_9;
    formula_9_ARG args9;
    args9.m = m;
    args9.lost_i = lost_index_i;
    args9.lost_j = lost_index_j;
    args9.diagonal_syndrome_names = formula_9_tmp_S1s;
    args9.formula_7_s_name = formula_7_tmp_S_cell_file_path;
    args9.diagonal_name = diagonal_strip_name;
    args9.data_names = origin_strip_names;
    pthread_create(&t_9, NULL, thread_formula_9, &args9);

    void* thread_result;
    pthread_join(t_8, &thread_result);
    pthread_join(t_9, &thread_result);

    recursive123(m, lost_index_i, lost_index_j,
                                    origin_strip_names[lost_index_i],
                                    origin_strip_names[lost_index_j],
                                    formula_8_tmp_S0s,
                                    formula_9_tmp_S1s);
    //删除临时文件
    removeTmpFile(formula_7_tmp_S_cell_file_path);
    for(int i=0; i<=m-1; i++){
        removeTmpFile(formula_8_tmp_S0s[i]);
        removeTmpFile(formula_9_tmp_S1s[i]);
    }

    //释放临时S0 和 S1的名字的空间
    formula_8_tmp_S0_name_free(m);
    formula_9_tmp_S1_name_free(m);
}

void repairFile(int file_id, int m, long padding_zero, int num1, int num2){
    /*
        修复情况：
        丢了1个disk，修复1个disk，num1有效，num2无效；
        丢了2个disk，只修复1个disk，num1有效，num2无效；
        丢了2个disk，修复2个disk，num1有效，num2有效；
    */
    // 有可能这个文件没丢失disk
    #ifndef RELEASE
    printf("file %d, m=%d, padding zero=%ld\n", file_id, m, padding_zero);
    #endif
    stripNameInit(m, file_id);
    lostScanM(m);

    switch (lost_num)
    {
        case 0:
            break;

        case 1:
            if(lost_disk_i == num1 || lost_disk_i == num2){
                if(lost_index_i <= m-1){
                    restoreOneLostDataStrip(origin_strip_names[lost_index_i], m, lost_index_i);
                }
                else if(lost_index_i == m){
                    writeRedundancyFileHorizontal(m);
                }
                else if(lost_index_i == m+1){
                    long cell_size = getFileSize(horizontal_strip_name) / (m-1);
                    writeSyndromeCellFile(cell_size, m);
                    writeRedundancyFileDiagonal(cell_size, m);
                    removeTmpFile(tmp_syndrome_file_path);
                }
            }
            break;

        case 2:
            if(lost_index_i == m && lost_index_j == m+1){
                #ifndef RELEASE
                printf("丢失两个校验列...\n", file_id);
                #endif
                if(lost_index_i == num1 || lost_index_i == num2){
                    // 恢复行校验列
                    writeRedundancyFileHorizontal(m);
                }
                if(lost_index_j == num1 || lost_index_j == num2){
                    // 恢复对角线校验列
                    long cell_size = getFileSize(horizontal_strip_name) / (m-1);
                    writeSyndromeCellFile(cell_size, m);
                    writeRedundancyFileDiagonal(cell_size, m);
                    removeTmpFile(tmp_syndrome_file_path);
                }
            }
            else if(lost_index_i < m && lost_index_j == m){
                if(lost_index_i == num1 || lost_index_i == num2){
                    // 恢复数据列
                    formula_5(m, lost_index_i);
                    formula_6(m, lost_index_i);
                    removeTmpFile(formula_5_tmp_S_cell_file_path);
                }
                if(lost_index_j == num1 || lost_index_j == num2){
                    // 恢复行校验列
                    writeRedundancyFileHorizontal(m); 
                }
            }
            else if(lost_index_i < m && lost_index_j == m+1){
                if(lost_index_i == num1 || lost_index_i == num2){
                    // 恢复数据列
                    restoreOneLostDataStrip(origin_strip_names[lost_index_i], m, lost_index_i);
                }
                if(lost_index_j == num1 || lost_index_j == num2){
                    // 恢复对角线校验列
                    long cell_size = getFileSize(horizontal_strip_name) / (m-1);
                    writeSyndromeCellFile(cell_size, m);
                    writeRedundancyFileDiagonal(cell_size, m);
                    removeTmpFile(tmp_syndrome_file_path);
                }
            }
            else if(lost_index_i < m && lost_index_j < m){
                #ifndef RELEASE
                printf("丢失两个数据列...\n", file_id);
                #endif
                repairTwoLostStrip(m, padding_zero);
            }
            break;

        default:
            printf("Impossible!\n");
            break;
    }
    freeStripNames(m);
}

void evenoddRepair(int repair_num1, int repair_num2){
    time_t seconds_start;
    seconds_start = time(NULL);

    FILE *fp = NULL; 
    if((fp = fopen(metaFilePath,"r")) == NULL){
        printf("evenoddRepair fopen error!\n");
        exit(-1);
    }

    int m = -1;
    long pad = -1;
    int meta_m[1024] = {0};
    long meta_pad[1024] = {0};
    int sum = 0;
    int max_m = 0;
    char meta_buf[128];
    while(1)
    {
        memset(meta_buf, 0, 128);
        fgets(meta_buf, 1024, fp);  //读取一行
        if(strlen(meta_buf) != 0)
        {
            m = getMfromMeta(meta_buf);
            pad = getPadfromMeta(meta_buf);
            meta_m[sum] = m;
            meta_pad[sum] = pad;
            max_m = m>max_m?m:max_m;
            sum++;
        }
        else
            break;
    }

    dataDiskFolderCheckAndMake(max_m);

    for(int i=0; i<=sum-1; i++)
        repairFile(i, meta_m[i], meta_pad[i], repair_num1, repair_num2);
        
    #ifndef RELEASE
    printf("文件共计%d个\n", sum);
    #endif
    fclose(fp);
    
    time_t seconds_end;
    seconds_end = time(NULL);
    #ifndef RELEASE
    printf("本次修复操作消耗 = %ld秒\n", seconds_end - seconds_start);
    #endif
}

int main(int argc, char** argv){
    // main
    if(argc < 2){
        usage();
        return -1;
    }

    metaTxtCheckAndMake();
    char* operation_type = argv[1];
    if(strcmp(operation_type, "write") == 0){
        int prime = atoi(argv[3]);
        evenoddWrite(argv[2], prime);
    }
    else if(strcmp(operation_type, "read") == 0){
        evenoddRead(argv[2], argv[3]);
    }
    else if(strcmp(operation_type, "repair") == 0){
        number_erasures =atoi(argv[2]);
        if(number_erasures <= 0){
            #ifndef RELEASE
            printf("修复数量不能小于等于0!\n");
            #endif
            exit(-1);
        }else if(number_erasures >= 3){
            printf("Too many corruptions!!\n");
            exit(-1);
        }else{
            if(number_erasures == 1){
                #ifndef RELEASE
                printf("修复丢失的disk%d...\n", atoi(argv[3]));
                #endif
                evenoddRepair(atoi(argv[3]), -1);
            }
            else if(number_erasures == 2){
                #ifndef RELEASE
                printf("修复丢失的disk%d disk%d...\n", atoi(argv[3]), atoi(argv[4]));
                #endif
                evenoddRepair(atoi(argv[3]), atoi(argv[4]));
            }  
        }
    }
    else{
        printf("Non-supported operations!\n");
    }
    return 0;
}