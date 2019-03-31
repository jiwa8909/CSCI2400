#include<iostream>
using namespace std;

int func4(int edx,int esi,int edi)
{
    //cout<<"1"<<endl;
    int ebx,eax;
    ebx = edx;
    ebx = ebx - esi;
    ebx = ebx >> 1;
    ebx = ebx + esi;
    if(ebx <= edi)
    {
        eax = ebx;
        if(ebx >= edi)
        {
            return eax;
        }
        esi = ebx + 1;
        eax = func4(edx,esi,edi) + ebx;
    }
    else
    {
        edx = ebx - 1;
        eax = func4(edx,esi,edi) + ebx;
    }

    return eax;
}

int main()
{
    int a = 0;
    int edx =  0xe;
    int esi = 0;
    for(int edi = 0; edi < 7; edi++)
    {
        a = func4(edx,esi,edi);
        cout<<edi<<"  "<<a<<endl;
    }

}
