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

//#define RELEASE
#define FILE_RIGHT (S_IRUSR | S_IWUSR | S_IXUSR |  S_IRGRP |  S_IWGRP |  S_IROTH | S_IWOTH)
#define HANG_JIAO_YAN_WRITE_BUF_SIZE (1024*1024*1024)
#define GB_SIZE (1024*1024*1024)
char* folderPrefix = "disk_";
char* metaFilePath = "meta.txt"; // <file_origin_path, P, paddingZeroNum>
char** origin_strip_names = NULL;
char* hang_strip_name = NULL;
char* dui_jiao_xian_strip_name = NULL;
char* tmp_syndrome_file_path = "syndrome";

void usage(){
    printf("./evenodd write <file_name> <p>\n");
    printf("./evenodd read <file_name> <save_as>\n");
    printf("./evenodd repair <number_erasures> <idx0>\n");
}

long getFileSize(char* file_path){
    int fd = open(file_path, O_RDONLY);
    if(fd < 0){
        printf("getFileSize open error, id %d, %s\n", errno, strerror(errno));
        exit(-1);
    }
    struct stat _stat;
    if(fstat(fd, &_stat) < 0){
        printf("getFileSize fstat error, id %d, %s\n", errno, strerror(errno));
        exit(-1);
    }
    return _stat.st_size; 
}

int getMeta(char* file_name, char* meta_buf){
    FILE *fp; 
    if((fp = fopen(metaFilePath,"r")) == NULL){
        printf("getMeta fopen error!\n");
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

int getPrimeFromMetaTxt(char* file_name){
    char read_buf[1024]={0}; 
    if(getMeta(file_name, read_buf) == -1){
        return -1;
    }

    int ans = 0;
    for(int i=strlen(file_name)+1; read_buf[i]!=' '; i++){
        ans += (read_buf[i]-'0');
        ans *= 10;    
    }
    ans /= 10;
    return ans;
}

int getPaddingZeroFromMetaTxt(char* file_name){
    char read_buf[1024]={0}; 
    if(getMeta(file_name, read_buf) == -1){
        return -1;
    }

    int padding_zero_start_pos = 0;
    int space_num = 0;
    for(int i=0; i<=strlen(read_buf)-1; i++){
        if(read_buf[i] == ' ')
            space_num++;
        if(space_num == 2){
            padding_zero_start_pos = (i+1);
            break;
        }
    }

    int ans = 0;
    for(int i=padding_zero_start_pos; read_buf[i]!='\n'; i++){
        ans += (read_buf[i]-'0');
        ans *= 10;
    }
    ans /= 10;
    return ans;
}

int cellNum(int p){
    return p*(p-1);
}

long cellSize(char* file_name, int p){
    double cn = cellNum(p);
    double si = getFileSize(file_name);
    return ceil(si / cn);
}

long stripSize(char* file_name, int p){
    return (p-1) * cellSize(file_name, p);
}

int lastStripPaddingZeroNum(char* file_name, int p){
    return stripSize(file_name, p) - getFileSize(file_name) % stripSize(file_name, p);
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
    if(fd < 0){
        printf("can not open file %s, %s\n", metaFilePath, strerror(errno));
        exit(-1);
    }
    close(fd);
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
    return ans;
}

void stripNameInit(int p, int file_id){
    char file_id_str[128]={0};
    sprintf(file_id_str, "%d", file_id);
    
    origin_strip_names=(char**)malloc(sizeof(char*)*p);
    hang_strip_name = (char*)malloc(sizeof(char)*128);
    dui_jiao_xian_strip_name = (char*)malloc(sizeof(char)*128);
    char* strip_name = NULL;

    char p_str[128]={0};
    for(int i=0; i<=p+1; i++){
        if(i == p){
            strip_name = hang_strip_name;
        }else if(i == (p+1)){
            strip_name = dui_jiao_xian_strip_name;
        }else{
            origin_strip_names[i] = (char*)malloc(sizeof(char)*128);
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
    if(fd_source < 0){
        printf("error %s\n", strerror(errno));
        exit(-1);
    }

    long strip_size = stripSize(file_path, p);
    for(int i=0; i<=p-1; i++){
        int fd_strip = open(origin_strip_names[i], O_RDWR | O_CREAT, FILE_RIGHT);
        if(fd_strip < 0){
            printf("strip %d error, %s\n", i, strerror(errno));
            exit(-1);
        }
        if(truncate(origin_strip_names[i], strip_size) < 0){
            printf("truncate error, %d, %s, errno %d\n", i, strerror(errno), errno);
            exit(-1);
        }

        lseek(fd_strip, 0, SEEK_SET);
        if(sendfile(fd_strip, fd_source, NULL, strip_size) == -1){
            printf("sendfile error, %d, %s, errno %d\n", i, strerror(errno), errno);
            exit(-1);
        }
        close(fd_strip);
    }
}

void XOR(char* buf1, char*buf2, int len){
    for(int i=0; i<=len-1; i++)
        buf1[i] = buf1[i]^buf2[i]; 
}

//行校验列
void writeRedundancyFileHang(char* file_path, int p){
    unsigned char* write_buf=(unsigned char*)malloc(HANG_JIAO_YAN_WRITE_BUF_SIZE);
    memset(write_buf, 0, HANG_JIAO_YAN_WRITE_BUF_SIZE);
    unsigned char* read_buf=(unsigned char*)malloc(HANG_JIAO_YAN_WRITE_BUF_SIZE);
    memset(read_buf, 0, HANG_JIAO_YAN_WRITE_BUF_SIZE);
    
    int* fds=(int*)malloc(p*sizeof(int));
    for(int i=0; i<=p-1; i++){
        fds[i] = open(origin_strip_names[i], O_RDONLY);
        if(fds[i] < 0){
            printf("open %s error, %s\n", origin_strip_names[i], strerror(errno)); 
            exit(-1);
        }
    }

    int hangFd = open(hang_strip_name, O_WRONLY | O_CREAT | O_APPEND, FILE_RIGHT);
    if(hangFd < 0){
        printf("open %s error, %s\n", hang_strip_name, strerror(errno)); 
        exit(-1);
    }

    while(1){
        int end = 0;
        int max_n = 0;
        for(int i=0; i<=p-1; i++){
            int n = read(fds[i], read_buf, HANG_JIAO_YAN_WRITE_BUF_SIZE);
            max_n = n>max_n?n:max_n;
            if(i==0 && n==0){
                end = 1;
                break;
            }
            XOR(write_buf, read_buf, n);
        }
        if(end==1)
            break;
        write(hangFd, write_buf, max_n);
    }

    long hang_strip_size = getFileSize(hang_strip_name);
    long strip_size = stripSize(file_path, p);
    if(hang_strip_size != strip_size){
        printf("hang strip generate error, size not valid\n"); 
        exit(-1);
    }

    for(int i=0; i<=p-1; i++)
        close(fds[i]);
    close(hangFd);
    free(fds);free(write_buf);free(read_buf);
}

//Syndrome
void writeSyndromeCellFile(char* file_path, int p){
    int fd_syn = open(tmp_syndrome_file_path, O_RDWR | O_APPEND | O_CREAT, FILE_RIGHT);
    unsigned char* write_buf = (unsigned char*)malloc(GB_SIZE);
    memset(write_buf, 0, GB_SIZE);
    unsigned char* read_buf = (unsigned char*)malloc(GB_SIZE);
    memset(read_buf, 0, GB_SIZE);
    
    int* fds=(int*)malloc(p*sizeof(int));
    long cell_size = cellSize(file_path, p);
    long rest_size = cell_size;
    for(int col=1; col<=p-1; col++){
        int row = p-col-1;
        fds[col] = open(origin_strip_names[col], O_RDONLY);
        lseek(fds[col], row*cell_size, SEEK_SET);    
    }

    while(1){
        int end = 0;
        int n = 0;
        for(int i=1; i<=p-1; i++){
            n = read(fds[i], read_buf, rest_size<GB_SIZE?rest_size:GB_SIZE);
            if(n == 0){
                end = 1;
                break;
            }
            XOR(write_buf, read_buf, n);
        }
        if(end==1)
            break;
        rest_size -= n;
        write(fd_syn, write_buf, n);
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
    free(fds);free(write_buf);free(read_buf);
}

void removeTmpSyndromeFile(){
    if(remove(tmp_syndrome_file_path) < 0){
        printf("remove syn fail\n");
        exit(-1);
    }
}


int mapToStripIndex(int row, int stripNumber){
    //<第几行, 使用到的第几个strip> -> strip index
    if(stripNumber > (row+1))
        return stripNumber;
    else
        return stripNumber-1;         
}

int mapToCellIndex(int row, int stripNumber, int p){
    //顺着strip往下推，推到第几个格子
    //stripNumber从1开始, row从0开始
    return (p+row-stripNumber)%(p-1);
}

//对角线校验列  
void writeRedundancyFileDuiJiaoXian(char* file_path, int p){
    writeSyndromeCellFile(file_path, p);
    unsigned char* write_buf = (unsigned char*)malloc(GB_SIZE);
    unsigned char* read_buf = (unsigned char*)malloc(GB_SIZE);

    int synFd = open(tmp_syndrome_file_path, O_RDONLY, FILE_RIGHT);
    if(synFd < 0){
        printf("open %s error, %s\n", tmp_syndrome_file_path, strerror(errno)); 
        exit(-1);
    }

    int duiFd = open(dui_jiao_xian_strip_name, O_WRONLY | O_CREAT | O_APPEND, FILE_RIGHT);
    if(duiFd < 0){
        printf("open %s error, %s\n", dui_jiao_xian_strip_name, strerror(errno)); 
        exit(-1);
    }

    int* fds=(int*)malloc(p*sizeof(int));
    for(int i=0; i<=p-1; i++){
        fds[i] = open(origin_strip_names[i], O_RDONLY);
    }
    
    long cell_size = cellSize(file_path, p);
    for(int i=0; i<=p-2; i++){
        // strip文件offset推到指定的位置去
        for(int j=1; j<=p-1; j++){
            int strip_index = mapToStripIndex(i, j);
            lseek(fds[strip_index], mapToCellIndex(i, j, p)*cell_size, SEEK_SET);
        }
        // syndrome文件offset回到起点
        lseek(synFd, 0, SEEK_SET);

        long rest_size = cell_size;
        while(1){
            int end = 0;
            int n = 0;
            //read strip
            for(int i=1; i<=p-1; i++){
                n = read(fds[i], read_buf, rest_size<GB_SIZE?rest_size:GB_SIZE);
                if(n == 0){
                    end = 1;
                    break;
                }
                XOR(write_buf, read_buf, n);
            }
            if(end==1)
                break;
            rest_size -= n;
            
            //read syndrom
            n = read(synFd, read_buf, rest_size<GB_SIZE?rest_size:GB_SIZE);
            XOR(write_buf, read_buf, n);

            //write
            write(duiFd, write_buf, n);
        }
    }

    close(duiFd);
    close(synFd);
    free(write_buf);
    free(read_buf);
    for(int i=0; i<=p-1; i++)   
        close(fds[i]);
    removeTmpSyndromeFile();
}

void evenoddWrite(char* file_path, int p){
    dataDiskFolderCheckAndMake(p);
    stripNameInit(p, getMetaNums());
    writeOriginStrip(file_path, p);
    writeRedundancyFileHang(file_path, p);
    writeRedundancyFileDuiJiaoXian(file_path, p);
}

/*
    丢失总结：
        1) 两个数据列
        2) 一个数据列，一个行校验列
        3) 一个数据列，一个对角线(Diagonal)校验列
        4) 两个校验列
        5) 丢失三个列或以上，不可恢复
*/
int existFile(const char* file_name){
    char meta_buf[128] = {0};
    if(getMeta(file_name, meta_buf) == -1)
        return false;
    return true;
}

int lostNum(const char* file_name){  
    // Todo 怎么合理地统计丢失的strip信息呢?
    char* meta_buf = (char*)malloc(sizeof(char) * 256);
    int file_id = getMeta(file_name, meta_buf);
    int p = getPrimeFromMetaTxt(file_name);
    stripNameInit(p, file_id);

    int lost_num = 0;
    for(int i=0; i<=p-1; i++){
        //如果访问不到这个列，说明，要么列文件丢了，要么整个disk目录丢了
        if(access(origin_strip_names[i], F_OK)  < 0)
            lost_num ++ ;
    }
    if(access(hang_strip_name, F_OK)  < 0)
            lost_num ++ ;
    if(access(dui_jiao_xian_strip_name, F_OK)  < 0)
            lost_num ++ ;

    return lost_num;
}

void evenoddRead(const char* file_name, const char* save_as){
    if(!existFile(file_name)){
        printf("File does not exist!\n");
        exit(-1);
    }

    if(lostNum(file_name) > 2){
        printf("File corrupted!\n");
        exit(-1);
    }
    //file name是存的时候的名字
    //save as是读的时候另存为的名字，这里另存为的地方应该是和evenodd同目录的地方
}

int main(int argc, char** argv){
    // unit test

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
        //todo
        ;
    }
    else{
        printf("Non-supported operations!\n");
    }
    return 0;
}