#include "parser.h"
#include <string>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fstream>
#include <fcntl.h>

using namespace std;

static int c;

typedef struct bundle{
    vector<char**> argvs;
} bundle;

// OK
void execute_single_bundle(bundle b)
{
    int N=b.argvs.size();
    for (int i=0; i<N; i++)
        if ( !fork() )
            execvp(b.argvs[i][0],b.argvs[i]);
    for(int i=0; i<N; i++)
        wait(&c);
    exit(0);
}

// OK
void execute_single_bundle_winput(bundle b, char* input)
{
    int N=b.argvs.size();
    for(int i=0; i<N; i++)
    {
        if ( !fork() )
        {
            int fd = open(input, O_RDONLY);
            dup2(fd,0);
            close(fd);
            execvp(b.argvs[i][0],b.argvs[i]);
        }
    }
    for(int i=0; i<N; i++)
        wait(&c);
    exit(0);
}

// OK
void execute_bundle_woutput(bundle b, char* output)
{
    int N=b.argvs.size();
    int fd = open(output,O_APPEND | O_CREAT | O_WRONLY);
    for(int i=0; i<N; i++)
    {
        if ( !fork() )
        {
            dup2(fd,1);
            close(fd);
            execvp(b.argvs[i][0],b.argvs[i]);
        }
    }
    for(int i=0; i<N; i++)
        wait(&c);
    close(fd);
    exit(0);
}

// OK
void execute_single_bothIO(bundle b, char* input, char* output)
{
    int N=b.argvs.size();
    int fd_out = open(output,O_APPEND | O_CREAT | O_WRONLY);
    for(int i=0; i<N; i++)
    {
        if ( !fork() )
        {
            dup2(fd_out,1);
            close(fd_out);
            int fd = open(input, O_RDONLY);
            dup2(fd,0);
            close(fd);
            execvp(b.argvs[i][0],b.argvs[i]);
        }
    }
    for(int i=0; i<N; i++)
        wait(&c);
    close(fd_out);
    exit(0);
}


void pipeline(unordered_map<string,bundle>& bs, bundle_execution* b_infos, int bundle_count){
    /*
    BRUTE FORCE SOLUTION..
    THIS PROCESS WILL CREATE AND CONTROL ALL FORKS AND REPEATERS
    !! IMPORTANT THING IS TO NOT FORGET CLOSING UNUSED PIPES...
    */
    int total_process = bs[string(b_infos[0].name)].argvs.size() + bundle_count - 1;
    // OPEN ALL THE PIPES
    int b_to_r[bundle_count-1][2]; // pipes from bundle to repeater
    int*** r_to_b = new int**[bundle_count-1];
    for(int i=0; i < bundle_count-1; i++)
    {
        pipe(b_to_r[i]);
        int inners = bs[string(b_infos[i+1].name)].argvs.size();
        total_process += inners;
        r_to_b[i] = new int*[inners];
        for(int j=0; j < inners; j++)
        {
            r_to_b[i][j] = new int[2];
            pipe(r_to_b[i][j]);
        }
    }

    // LETS FORK & EXECUTE ALL PROCESSES...
    
    // first bundle
    int number_process = bs[string(b_infos[0].name)].argvs.size();
    for(int j=0; j < number_process; j++)
    {   
        if ( !fork() )
        {
            // set necessary pipes
            if (b_infos[0].input)
            {
                int inp_fd = open(b_infos[0].input, O_RDONLY);
                dup2(inp_fd,0);
                close(inp_fd);
            }
            close(b_to_r[0][0]);
            dup2(b_to_r[0][1], 1);
            close(b_to_r[0][1]);
            
            // && close unused pipes !!!
            for(int k=0; k<bundle_count-1; k++)
            {
                if(k)
                {
                    close(b_to_r[k][0]);
                    close(b_to_r[k][1]);
                }
                int temp = bs[string(b_infos[k+1].name)].argvs.size();
                for(int kk=0; kk<temp; kk++)
                {
                    close(r_to_b[k][kk][0]);
                    close(r_to_b[k][kk][1]);
                }
            }
            execvp(bs[string(b_infos[0].name)].argvs[j][0],bs[string(b_infos[0].name)].argvs[j]);
        }
    }
    
    // bundles from 2 to N-1
    for(int i=1; i < bundle_count-1; i++)
    {
        int number_inners = bs[string(b_infos[i].name)].argvs.size();
        for(int j=0; j < number_inners; j++)
        {   
            if ( !fork() )
            {
                // use necessary pipes
                close(b_to_r[i][0]);
                dup2(b_to_r[i][1], 1);
                close(b_to_r[i][1]);
                //
                close(r_to_b[i-1][j][1]);
                dup2(r_to_b[i-1][j][0], 0);
                close(r_to_b[i-1][j][0]);
                // && close unused pipes !!!
                for(int k=0; k<bundle_count-1; k++)
                {
                    if ( k != i)
                    {
                        close(b_to_r[k][0]);
                        close(b_to_r[k][1]);
                    }
                    if ( k != i-1)
                    {
                        int temp = bs[string(b_infos[k+1].name)].argvs.size();
                        for(int kk=0; kk<temp; kk++)
                        {
                            close(r_to_b[k][kk][0]);
                            close(r_to_b[k][kk][1]);
                        }

                    }
                    if (k == i-1)
                    {
                        for(int kk=0; kk<number_inners; kk++)
                        {
                            if(kk!=j)
                            {
                                close(r_to_b[k][kk][0]);
                                close(r_to_b[k][kk][1]);
                            }
                        }  
                    }
                }
                execvp(bs[string(b_infos[i].name)].argvs[j][0],bs[string(b_infos[i].name)].argvs[j]);
            }
        }
    }

    // Nth bundle
    number_process = bs[string(b_infos[bundle_count-1].name)].argvs.size();
    int out_fd = -1;
    if (b_infos[bundle_count-1].output)
    {
        out_fd = open(b_infos[bundle_count-1].output,O_APPEND | O_CREAT | O_WRONLY);        
    }
    for(int j=0; j < number_process; j++)
    {   
        if ( !fork() )
        {
            // use necessary pipes
            if (b_infos[bundle_count-1].output)
            {
                dup2(out_fd,1);
                close(out_fd);
            }
            close(r_to_b[bundle_count-2][j][1]);
            dup2(r_to_b[bundle_count-2][j][0], 0);
            close(r_to_b[bundle_count-2][j][0]);

            // && close unused pipes !!!
            close(b_to_r[bundle_count-2][0]);
            close(b_to_r[bundle_count-2][1]);
            for(int i=0; i < number_process; i++)
            {
                if(i!=j)
                {
                    close(r_to_b[bundle_count-2][i][0]);
                    close(r_to_b[bundle_count-2][i][1]);
                }
            } 
            for(int k=0; k<bundle_count-2; k++)
            {
                close(b_to_r[k][0]);
                close(b_to_r[k][1]);
                int temp = bs[string(b_infos[k+1].name)].argvs.size();
                for(int kk=0; kk<temp; kk++)
                {
                    close(r_to_b[k][kk][0]);
                    close(r_to_b[k][kk][1]);
                }
            }
            execvp(bs[string(b_infos[bundle_count-1].name)].argvs[j][0],bs[string(b_infos[bundle_count-1].name)].argvs[j]);
        }
    }
    /// CREATE REPEATERS
    for(int i=0; i < bundle_count-1; i++)
    {
        int number_forward = bs[string(b_infos[i+1].name)].argvs.size();
        if ( !fork() )
        {
            // get necesarry pipe
            close(b_to_r[i][1]);
            dup2(b_to_r[i][0],0);
            close(b_to_r[i][0]);
            // r_to_b pipes gonna be redirected later when outputting

            // && close unused pipes !!!
            for(int k=0; k<bundle_count-1; k++)
            {
                if ( k != i ) 
                {
                    close(b_to_r[k][0]);
                    close(b_to_r[k][1]);
                    int temp = bs[string(b_infos[k+1].name)].argvs.size();
                    for(int kk=0; kk<temp; kk++)
                    {
                        close(r_to_b[k][kk][0]);
                        close(r_to_b[k][kk][1]);
                    }
                }
            }

            // wait for inputs and stream them
            //string tmp;
            char tmp;
            int count = 0;
            int number_back = bs[string(b_infos[i].name)].argvs.size();
            int eof = 1;
            while(count != number_back)
            {
                eof = read(0, &tmp, 1);
                if(!eof)
                    count++;
                else
                {
                    for(int p=0; p<number_forward; p++)
                    {
                        dup2(r_to_b[i][p][1],1);
                        write(1, &tmp, 1);
                    }
                }
            }
            close(b_to_r[i][0]);
            close(b_to_r[i][1]);
            for(int l=0; l<number_forward; l++){
                close(r_to_b[i][l][1]);
                close(r_to_b[i][l][0]);
            }
            close(1);
            close(0);
            exit(0);
        }
    }
    // CLOSE ALL THE PIPES IN THE GENERATOR...
    for(int i=0; i<bundle_count-1; i++)
    {
        close(b_to_r[i][0]);
        close(b_to_r[i][1]);
        int len = bs[string(b_infos[i+1].name)].argvs.size();
        for(int j=0; j<len; j++)
        {
            close(r_to_b[i][j][1]);
            close(r_to_b[i][j][0]);
            delete [] r_to_b[i][j];
        }
        delete [] r_to_b[i];
    }
    delete [] r_to_b;
    close(1);
    close(0);
    for(int i=0; i<total_process; i++)
        wait(&c);
    exit(0);
}

int main()
{
    parsed_input inp;
    char in_creation = 0;
    char* buffer = new char[255];
    string executed;
    unordered_map<string,bundle> bundles;
    bundle temp;
    while(true)
    {
        cin.getline(buffer,255,'\n');
        string mybuff(buffer);
        mybuff.push_back('\n');
        mybuff.push_back('\0');
        int stopped = parse(&mybuff[0], in_creation, &inp);
        if (inp.command.type == PROCESS_BUNDLE_CREATE)
        {
            in_creation = 1;
            executed = string(inp.command.bundle_name);
            bundles[executed] = bundle();
        }
        else if (stopped || inp.command.type == PROCESS_BUNDLE_STOP)
        {
            in_creation = 0;
        }
        else if (inp.command.type == PROCESS_BUNDLE_EXECUTION)
        {
            if (inp.command.bundle_count == 1)
            {
                if (inp.command.bundles[0].output == NULL && inp.command.bundles[0].input == NULL)
                {
                    if (!fork())
                        execute_single_bundle(bundles[inp.command.bundles[0].name]);
                    else
                        wait(&c);
                }
                else if (inp.command.bundles[0].output != NULL && inp.command.bundles[0].input == NULL)
                {
                    if (!fork())
                        execute_bundle_woutput(bundles[inp.command.bundles[0].name],inp.command.bundles[0].output);
                    else
                        wait(&c);
                }
                else if (inp.command.bundles[0].output == NULL && inp.command.bundles[0].input != NULL)
                {
                    if (!fork())
                        execute_single_bundle_winput(bundles[inp.command.bundles[0].name],inp.command.bundles[0].input);
                    else
                        wait(&c);
                }
                else
                {
                    if (!fork())
                        execute_single_bothIO(bundles[inp.command.bundles[0].name], inp.command.bundles[0].input, inp.command.bundles[0].output);
                    else
                        wait(&c);
                }
            }
            else
            {
                if (!fork())
                    pipeline(bundles, inp.command.bundles, inp.command.bundle_count);
                else
                    wait(&c);
            }
            for(int i=0; i< inp.command.bundle_count; i++)
                bundles.erase(inp.command.bundles[i].name);
        }
        else if (inp.command.type == QUIT)
        {
            delete [] buffer;
            exit(0);
        }
        else
        {
            bundles[executed].argvs.push_back(inp.argv);
        }
    }
    return 0;
}