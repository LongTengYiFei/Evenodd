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

#define FILE_RIGHT (S_IRUSR | S_IWUSR | S_IXUSR |  S_IRGRP |  S_IWGRP |  S_IROTH | S_IWOTH)
char* folderPrefix = "disk_";
char* metaFilePath = "meta.txt"; // <file_origin_path, P, paddingZeroNum>

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
            return 0;
        }
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

void writeOriginStrip(char* file_path, int p){
    //数据列
    int fd_source = open(file_path, O_RDONLY);
    if(fd_source < 0){
        printf("error %s\n", strerror(errno));
        exit(-1);
    }

    int file_id = getMetaNums();
    char file_id_str[128]={0};
    sprintf(file_id_str, "%d", file_id);

    long strip_size = stripSize(file_path, p);
    char strip_name[128]={0};
    char p_str[128]={0};
    
    for(int i=0; i<=p-1; i++){
        memset(p_str, 0, 128);
        sprintf(p_str, "%d", i);

        memset(strip_name, 0, 128);
        memcpy(strip_name, folderPrefix, strlen(folderPrefix));
        memcpy(strip_name+strlen(folderPrefix), p_str, strlen(p_str));
        memcpy(strip_name+strlen(folderPrefix)+strlen(p_str), "/", 1);
        memcpy(strip_name+strlen(folderPrefix)+strlen(p_str)+1, file_id_str, strlen(file_id_str));

        int fd_strip = open(strip_name, O_RDWR | O_CREAT, FILE_RIGHT);
        if(fd_strip < 0){
            printf("strip %d error, %s\n", i, strerror(errno));
            exit(-1);
        }
        if(truncate(strip_name, strip_size) < 0){
            printf("truncate error, %d, %s, errno %d\n", i, strerror(errno), errno);
            exit(-1);
        }

        lseek(fd_strip, 0, SEEK_SET);
        if(sendfile(fd_strip, fd_source, NULL, strip_size) == -1){
            printf("sendfile error, %d, %s, errno %d\n", i, strerror(errno), errno);
            exit(-1);
        }
    }
}

void writeRedundancyFile1(char* file_path, int p){
    //行校验列
}

void writeRedundancyFile2(char* file_path, int p){
    //对角线校验列
}

void evenoddWrite(char* file_path, int p){
    dataDiskFolderCheckAndMake(p);
    writeOriginStrip(file_path, p);
    writeRedundancyFile1(file_path, p);
    writeRedundancyFile2(file_path, p);
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
        //todo
        ;
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