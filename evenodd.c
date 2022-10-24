#include <stdio.h>
#include <string.h>

void evenoddWrite();
void evenoddRead();
void evenoddRepair();

void usage(){
    printf("./evenodd write <file_name> <p>\n");
    printf("./evenodd read <file_name> <save_as>\n");
    printf("./evenodd repair <number_erasures> <idx0>\n");
}

int main(int argc, char** argv){
    if(argc < 2){
        usage();
        return -1;
    }

    char* operation_type = argv[1];
    if(strcmp(operation_type, "write") == 0){
        evenoddWrite();
    }else if(strcmp(operation_type, "read") == 0){
        evenoddRead();
    }else if(strcmp(operation_type, "repair") == 0){
        evenoddRepair();
    }else{
        printf("Non-supported operations!\n");
    }
    return 0;
}


