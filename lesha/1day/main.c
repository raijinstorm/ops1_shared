#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Simple macros definition:
#define max(a,b) a>b ? a:b


int str_length(char* somestr)
{
    if (somestr==NULL)
        return 0;
    int number=0;
    while(somestr[number]!='\0')
    {
        number+=1;
    }
    return number;
}

char* str_copy(char* somestr)
{
    if (somestr==NULL)
        return NULL;
    int length=str_length(somestr);
    char* new_str=malloc(sizeof(char)*length);
    for (int i=0; i<length; i++)
    {
        new_str[i]=somestr[i];
    }
    return new_str;
}

int str_cmp(char* somestr1, char* somestr2)
{
    if (somestr1==NULL || somestr2==NULL)
    {
        exit(322);
    }
    int length1=str_length(somestr1);
    int length2=str_length(somestr2);
    if (length1!=length2)
        return length1<length2 ? -1:1;
    for (int i=0; i<length1; i++)
    {
        if (somestr1[i]!=somestr2[i])
        {
            return (int) (somestr2[i]-somestr1[i]);
        }
    }
    return 0;

}

int str_reverse(char* somestring) {
    if (somestring==NULL)
    {
        return 0;
    }
    int length=strlen(somestring);
    for (int i=0;i<(int)(length/2);i++)
    {
        char temp=somestring[i];
        somestring[i]= somestring[length-1-i];
        somestring[length-1-i]=temp;
    }
    return 1;
}

int str_remove_spaces(char *somestring) {
    int length=strlen(somestring);
    if (length==0)
        return 0;

    for (int i=0;i<length;i++)
    {
        if (somestring[i]==' ')
        {
            for (int j=i; j<length; j++) {
                somestring[j]=somestring[j+1];
            }
            length--;
            i--;
        }
    }
}

int str_is_palindrome(char* somestring)
{
    int length=str_length(somestring);
    for (int i=0;i<length/2;i++) {
        if (somestring[i]!=somestring[length-1-i])
            return 0;
    }
    return 1;
}


int main(void) {


    // //  Simple i/o:
    // int a,b;
    // printf("Give the first number: ");
    // scanf("%d",&a);
    // printf("Give the second number: ");
    // scanf("%d",&b);
    // printf("Maximal number out of: %d and %d is %d",a,b,max(a,b));


    // //  Simple string manipulation:
    // char str[10];
    // puts("Now type some string:");
    // scanf("%9s",str);
    // printf("You had typed: %s\n",str);
    // for (int i=0;i<strlen(str);i++) {
    //     printf("%c ",str[i]);
    //     if (i+1!=strlen(str)) {
    //         printf(", ");
    //     }
    //     else {
    //         printf("\n");
    //     }
    // }
    //
    // puts("Or equivalently with null terminator:\n");
    // for (int i=0;str[i]!='\0';i++) {
    //     printf("%d) is %c",i+1,str[i]);
    //     if (i+1<10 && str[i+1]!='\0') {
    //         puts(";");
    //     }
    //     else {
    //         puts("");
    //     }
    // }

    //  //  Counting number of letters
    // char str[100];
    // printf("Give a word (<100 chars): ");
    // fgets(str,sizeof(str),stdin);
    //
    // printf("The length of your string is %d",str_length(str));


    //  Copying string
    // char somestr[100];
    // printf("Enter a string: ");
    // fgets(somestr,(int)(sizeof(somestr)/sizeof(char)),stdin);
    // char * new_string=str_copy(somestr);
    // printf("Copied string: %s",new_string);
    // free(new_string);
    // printf("Output: %d",str_cmp("\6","\n"));

    // //  Comparing strings
    // char somestr1[]="hello_world";
    // str_reverse(somestr1);
    // printf("%s",somestr1);


    // //  Removing spaces in string
    // // char somestring[]="A text with a lot of spaces";
    // // char somestring[]="        1 2 3      ";
    // str_remove_spaces(somestring);
    // printf("%s\n with length %d",somestring,strlen(somestring));

    char* result=str_is_palindrome("\0") ? "String is palindrome" : "Not palindrome";
    printf("%s",result);
    return 0;
}