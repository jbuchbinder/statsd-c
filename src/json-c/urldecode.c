#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#define URLDECODE_STATE_SEARCH 0
#define URLDECODE_STATE_CONVERTING 1

void urldecode(char *pszDecodedOut, size_t nBufferSize, const char *pszEncodedIn)
{
    memset(pszDecodedOut, 0, nBufferSize);

    int state = URLDECODE_STATE_SEARCH;

    unsigned int i;
    for(i = 0; i < strlen(pszEncodedIn)-1; ++i)
    {
        switch(state)
        {
        case URLDECODE_STATE_SEARCH:
            {
                if(pszEncodedIn[i] != '%')
                {
                    strncat(pszDecodedOut, &pszEncodedIn[i], 1);
                    break;
                }

                // We are now converting
                state = URLDECODE_STATE_CONVERTING;
            }
            break;

        case URLDECODE_STATE_CONVERTING:
            {
                // Conversion complete (i.e. don't convert again next iter)
                state = URLDECODE_STATE_SEARCH;

                // Create a buffer to hold the hex. For example, if %20, this
                // buffer would hold 20 (in ASCII)
                char pszTempNumBuf[3] = {0};
                strncpy(pszTempNumBuf, &pszEncodedIn[i], 2);

                // Ensure both characters are hexadecimal
                bool bBothDigits = true;

                int j;
                for(j = 0; j < 2; ++j)
                {
                    if(!isxdigit(pszTempNumBuf[j]))
                        bBothDigits = false;
                }

                if(!bBothDigits)
                    break;

                // Convert two hexadecimal characters into one character
                int nAsciiCharacter;
                sscanf(pszTempNumBuf, "%x", &nAsciiCharacter);

                // Ensure we aren't going to overflow
                assert(strlen(pszDecodedOut) < nBufferSize);

                // Concatenate this character onto the output
                strncat(pszDecodedOut, (char*)&nAsciiCharacter, 1);

                // Skip the next character
                i++;
            }
            break;
        }
    }
}

