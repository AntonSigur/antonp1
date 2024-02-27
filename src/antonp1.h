 /*
 This is Anton Sigurjónsson implementation of reading out P1 port of Icelandic Iskraemeco meters and making the data available in a 
 structured way based on OBIS codes from the meter. No hardcoded OBIS codes - raw parsing only.

 p1ReadAndParseNow() function requests data from meter and parses it in the structure.

 The structure can then be accessed and used from the list:
 OBISItem* item = p1parsed->items;

 */
#include <Arduino.h>
#include "RemoteDebug.h"
#define P1_REQUEST_PIN D5
#define P1_MAXBUFFER 750 
#define P1_READ_INTERVAL 2000 //Refresh P1 data every X seconds

char P1buffer[P1_MAXBUFFER];
int P1length=0;
boolean P1valid = false;
String P1error="";
unsigned long P1NextMillis = 0;
static uint16_t parsingobis[5]; //Temporary device+obis-code parsing array.

/*
Simple telegram message:
/ISk5\2MIE5E-200

0-0:96.1.0(84035454)
0-0:96.1.1(36303834303335343534) <- device identification number
1-0:0.9.1(155237)
1-0:0.9.2(230510)
1-0:1.8.0(000002.331*kWh)
1-0:2.8.0(000000.000*kWh)
1-0:1.7.0(00.000*kW)
1-0:2.7.0(00.000*kW)
1-0:3.7.0(00.000*kvar)
1-0:4.7.0(00.000*kvar)
1-0:31.7.0(000*A)
1-0:32.7.0(230.9*V)
!
*/

/*
OBISUnit holds all different OBIS units in the OBISItems. 
This ensures lower memory usage, as we only need to declare a char* once for each different unit
Multiple items are likely to share the same units. 
*/
class OBISUnit
{
  public:
  char* unitstr;
  OBISUnit* next;

  ~OBISUnit()
  {
      if (unitstr != nullptr) {
          delete[] unitstr;
      }
  }    
};

OBISUnit* unitList;


int countDigits(int num) {
    if (num == 0) {
        return 1;  // Special case for zero
    }
    
    int count = 0;
    while (num != 0) {
        num /= 10;
        count++;
    }
    
    return count;
}

class OBISItem
{
    private:
      char* obisString;
    public:
    uint16_t obis[5]; //The device ID pointer, e.g. 1-0 and then obis code, e.g. 31.7.2

    enum ValueType {NONE=0,DOUBLE=1, INT32=2, INT64=3, TIME=4,CHARARR=5};
    union Value {
      double dValue;
      uint32_t i32Value;
      uint64_t i64Value;
      char* stringValue;
    };
    Value value;
    ValueType type;
    OBISUnit* unit;
 
    OBISItem* next;

    ~OBISItem()
    {
        if (type == CHARARR && value.stringValue != nullptr) {
            delete[] value.stringValue;
        }
    }

    /*
      Returns the OBIS code - note, only the obis code e.g. 1.8.0, not the whole with device id's etc
      The code is created, if it is not part of the object, and stored.
    */
    char* getObisCode()
    {
      if (obisString == nullptr)
      {
        int whatSize = 3; //two Dots + terminator
        whatSize += countDigits(obis[2]);
        whatSize += countDigits(obis[3]);
        whatSize += countDigits(obis[4]);

        obisString = new char[whatSize]; //"1-1:96.96.96"
        snprintf(obisString, whatSize, "%d.%d.%d",obis[2],obis[3],obis[4]);
      }

      return obisString;     
    }    

    void setUnitType(const char* array, int startIndex, int endIndex)
    {
      int size = endIndex - startIndex;
      OBISUnit* unitl = unitList;
      while (unitl != nullptr)   //Finding existing declared unit
      {     
        if (strlen(unitl->unitstr) == size)
        {
          bool found = true;
          for (int i = 0 ; i<size; i++)
          {
            if (unitl->unitstr[i] != array[startIndex + i])
            {
              found = false;
              break;
            }
          }
          if (found)
          {
            unit = unitl;
            return;
          }
        }
        unitl = unitl->next;
      }
      //Creating new unit in unit list
      OBISUnit* newUnit = new OBISUnit();
      newUnit->unitstr = new char[size + 1];
      for (int i = 0 ; i<size; i++)
      {
        newUnit->unitstr[i] = array[startIndex + i];
      }
      newUnit->unitstr[size] = '\0';
      newUnit->next = unitList;
      unitList = newUnit;
      unit = newUnit;
    }
};

bool compareObisArrays(uint16_t (&ob1)[5], uint16_t (&ob2)[5])
{
  for (int i = 0; i<5;i++)
  {
    if (ob1[i] != ob2[i]) 
    {
      return false;
    }
  }
  return true;
}

class ParsedOBIS
{
  public:
  OBISItem* items;
  OBISItem* findOrCreatOBISItem(uint16_t (&obcode)[5])
  {
    OBISItem* item = items;
    while (item != nullptr)
    {
      if (compareObisArrays(obcode,item->obis))
      {
        return item;
      }
      item = item->next;
    }

    //Not found, create new
    item = new OBISItem();
    for (int r = 0; r<5;r++)
    {
      item->obis[r] = obcode[r];
    }

    item->next = items;
    items = item;
    return item;
  }
};



ParsedOBIS* p1parsed = new ParsedOBIS();


/*
Setup of the serial communication and inputs.
TODO: Add as a configuration/input parameters to a setup function
*/
void p1setup() 
{
    pinMode(P1_REQUEST_PIN, OUTPUT); //Request pin!
    pinMode(D7,INPUT); //Using the "other Serial HW pins"
    Serial.begin(9600,SERIAL_7N1,SERIAL_RX_ONLY);
    Serial.swap();
    Serial.flush();
    P1NextMillis = millis() + P1_READ_INTERVAL;
}

/*
Return integer value of char, if not integer value, return -1 (not a valid value for a single char)
It will also set isValid reference to false 
it never sets it to true and should not do that! - as for repeated calls the final results can be evaluated with single check in caller function
Note: this is the most "optimized" way for microcontroler to convert but could be replaced by other functions...
*/
int getInteger(const char& c, bool& isValid)
{
    switch (c)
    {
    case '0':
      return 0; 
    case '1':
      return 1; 
    case '2':
      return 2; 
    case '3':
      return 3; 
    case '4':
      return 4; 
    case '5':
      return 5; 
    case '6':
      return 6; 
    case '7':
      return 7;                                           
    case '8':
      return 8; 
    case '9':
      return 9; 
    default:
      isValid = false;
      return -1;
    }
}

/*
Compares two OBIS items by it's code
*/
bool compareObisCode(OBISItem *item, OBISItem *pi)
{
  for (int i =0;i<5;i++)
  {
    if (item->obis[i] != pi->obis[i])
      return false;
  }

  return true;
}


void parseStrArrIntoItem(const char* array, int startIndex, int endIndex, OBISItem *item) 
{
  int newSize = endIndex - startIndex + 1;

  if (item->type == OBISItem::CHARARR  && strlen(item->value.stringValue) == newSize) //We already have the itme definded with some CHARARR items
  {
    for (int i = startIndex; i <= endIndex; i++) 
    {
      item->value.stringValue[i - startIndex] = array[i];
    }
    item->value.stringValue[newSize] = '\0'; // Null-terminate the string
    return;
  } 

  if (item->type == OBISItem::CHARARR && item->value.stringValue != nullptr) 
  {
    delete[] item->value.stringValue;
  }

  item->value.stringValue = new char[newSize + 1];
  item->type = OBISItem::CHARARR;

  for (int i = startIndex; i <= endIndex; i++) 
  {
    item->value.stringValue[i - startIndex] = array[i];
  }
  item->value.stringValue[newSize] = '\0'; // Null-terminate the string
}

/*
  This parse function assumes only positive double values and updates isValid=false if array contains other than only digits and single dot.
*/
double parseDoubleFromArray(const char* array, int startIndex, int endIndex, bool& isValid) {

    double result = 0.0;
    int i = startIndex;
    for (; i <= endIndex; i++) 
    {
          if (array[i] == '.') 
          {
            i++;
            double factor = 1.0;
            for (; i <= endIndex; i++) 
            {
              factor *= 0.1;            
              result += (getInteger(array[i],isValid)*factor);
            }
            break;
          }
        result = result * 10.0 + getInteger(array[i],isValid);
    }
    return result;
}

uint32_t parseInt32FromArray(const char* array, int startIndex, int endIndex, bool& isValid) {
    uint32_t result = 0;
    int i = startIndex;
    for (; i <= endIndex; i++) 
    {
        result = result * 10.0 + getInteger(array[i],isValid);
    }
    return result;
}

uint64_t parseInt64FromArray(const char* array, int startIndex, int endIndex, bool& isValid) {
    uint64_t result = 0;
    int i = startIndex;
    for (; i <= endIndex; i++) 
    {
        result = result * 10.0 + getInteger(array[i],isValid);
    }
    return result;
}

/*
  This parse function assumes only positive value and updates isValid=false if array contains other than only digits and single dot. 
*/
bool isOnlyDigitsInArray(const char* array, int startIndex, int endIndex) {
    int i = startIndex;
    bool isValid = true;
    for (; i <= endIndex; i++) 
    {
      getInteger(array[i],isValid);
      if (!isValid)
        break;
    }
    return isValid;
}

void parseItems()
{
  uint16_t linepos = 0; //The position within a new-line. Starting as 0 and reset when detecting \c\r
  bool failParseLine = false;
  int i = 0;
  OBISItem* parsingItem = nullptr;
  while (i<P1length) 
  {
    bool isValid = true;
    if (!failParseLine) {

      //Parse DeviceID
      if (linepos == 0 || linepos == 2) 
      {
        int x = getInteger(P1buffer[i], isValid);        
        if (isValid)
        {
          if (linepos == 0)
          {
            parsingobis[0] = x;
          }
          else
          {
            parsingobis[1] = x;
          }
        }
        else //We only parse out actual OBIS codes, not other lines.
        {
          failParseLine = true;
          //debugE("The device identity was not parsed out of line, %i - as %i",linepos,x);
        }
      }
      if (linepos == 1 && P1buffer[i] != '-')
      {
        failParseLine = true;
        //debugE("The device identity error , no dash '-',%c",P1buffer[i]);
      }

      //Parse OBIS code info
      if (linepos == 3 && P1buffer[i] == ':') //We are in the obis-coding , e.g. 1.85.5 - to parse into obis array
      {
        int obisPos = 0; //Starting with the first obis code digits
        parsingobis[2] = 0; //Init a zero
        parsingobis[3] = 0; //Init a zero
        parsingobis[4] = 0; //Init a zero
        i++; linepos++; //Move over the ':'
        while (P1buffer[i] != '(') //Until we are at the end of OBIS code declaration in message
        {
          if (obisPos > 2) { //Obis codes are only three integers. Something is not parsing correctly.
            debugE("To long OBIS code?? should only have 3 digits: %i.%i.%i",parsingobis[2],parsingobis[3],parsingobis[4]);
            failParseLine = true;
            break;
          }
          isValid = true;
          int y = getInteger(P1buffer[i], isValid);
          while (P1buffer[i] != '.' && isValid)
          {
            //debugI("Parsed Obis Integer: %i from char %c",y,P1buffer[i]);     
            if (isValid) 
            {
              parsingobis[obisPos+2] *= 10;
              parsingobis[obisPos+2] += y;
            } 
            else
            {
              debugE("Not valid integer value in %i",i);
              failParseLine = true;
              break;
            }
            i++; linepos++;
            y = getInteger(P1buffer[i],isValid);
          }
          if (P1buffer[i] == '.')
          {
            obisPos++; //Move to next OBIS POSITION
            i++; linepos++; //Move over the DOT '.' position
          }
        }
      } 
      else if(linepos == 3) 
      {
        failParseLine = true;
        debugE("char no 3 is not a colon ':'");
      }

      if (P1buffer[i] == '(' && (parsingobis[2] + parsingobis[3] + parsingobis[4] > 0)) //Value for OBIS code parsing starts and we have a valid OBIS
      {
        //debugI("Found value start (bracket)");
        i++; //Jump over the bracket '('
        int le = 0;
        int dotPos = -1;
        int starPos = -1;
        int endPos = -1;

        //Find parsingObis item or create;
        parsingItem =  p1parsed->findOrCreatOBISItem(parsingobis);



        //Finding end position of value. Try detect value type.
        char t = P1buffer[i+le];
        while (endPos == -1 && t != '\n' && i+le<P1length)
        {
          if (t == '.')
          {
            dotPos = i+le;
          }
          else if (t == '*')
          {
            starPos = i+le;
          }
          le++;
          t = P1buffer[i+le];
          if (t == ')') 
          {
            endPos = i+le;
          }
        } //endFind

        int pos = endPos - 1; //EndPosition for the value within brackets  () or before star * , eg (123.456*xyz) = 123.456 positions
        if (starPos > 0)
        {
          pos = starPos -1;
        }
        
        if (endPos > 0) //Start parsing values
        {
          isValid = true;
          if (dotPos > 0) //value with decimal point, (try)parse as double
          {
            parsingItem->value.dValue = parseDoubleFromArray(P1buffer,i,pos,isValid);
            parsingItem->type = OBISItem::DOUBLE;
            if (!isValid)
            {
              parseStrArrIntoItem(P1buffer,i,pos,parsingItem); //sets the string value into item and handles memory&fragmentation
            }
            //debugI("found a value of %f - valid: %i",parsingItem->dValue,isValid);
          }
          else 
          {
            if (isOnlyDigitsInArray(P1buffer,i,pos))
            {
              //Using "good enough" data type detection for scenarios in OBIS codes. 
              if (endPos-i > 18) //We parse as string/none?
              {
                parseStrArrIntoItem(P1buffer,i,pos,parsingItem); //sets the string value into item and handles memory&fragmentation
              } else if (endPos-i < 10) //We parse as int32
              {
                parsingItem->value.i32Value = parseInt32FromArray(P1buffer,i,pos,isValid);
                parsingItem->type = OBISItem::INT32;                
              } else //We parse as 64
              {
                parsingItem->value.i64Value = parseInt64FromArray(P1buffer,i,pos,isValid);
                parsingItem->type = OBISItem::INT64;   
              }
            }
            else
            {
              parseStrArrIntoItem(P1buffer,i,pos,parsingItem); //sets the string value into item and handles memory&fragmentation
            }
          }
        }
        else 
        {
          failParseLine = true;
        }

        /*
          Unit handling for OBIS. 
          Assumption. Unit of a given OBIS code should never change. No need to re-update (MC restart would ofc. refresh)
        */
        if (endPos > 0 && starPos > 0 && parsingItem->unit == nullptr)
        {
          parsingItem->setUnitType(P1buffer, starPos+1, endPos);
        }

      }
    }

    //Detecting a new line, now, find next line and reset get ready for parsing.
    if (P1buffer[i] == '\r') 
    {
      //debugI("Detecting new line at buffer %i -- %c%c",i,P1buffer[i],P1buffer[i+1]);
      //debugHandle();
      if (i+1<P1length)
      {
        if (P1buffer[i+1] == '\n')
         i++;
      }

      if (!failParseLine) //We have a parsed line successfully.
      {
      }
      failParseLine = false;
      linepos = -1;
    }

    i++; linepos++;
  }
}


/*
Handles the serial and message parsing from serial into a buffer.
*/
boolean P1_ReadFromSerial(char *b,int *l)
{
  char c;
  char *buffer;
  //char bcrc[4];
  int t=0;
  long timer;
  timer = millis();

  // Parsing Flags
  boolean message_start=false; //<-- what is this
  boolean message_end=false;
  boolean  message_error = false;
  P1error="";
  // initialise counter and string buffer
  buffer=b;

  Serial.flush();
  digitalWrite(P1_REQUEST_PIN,HIGH);

  while (!message_end && !message_error) // find message start
  {
    if (millis()-timer > 12000) 
    {
      message_error=true; 
      P1error="Time out";
      break;
    } 
    if (Serial.available()) 
    {
      c= Serial.read();
      if (c == '/')  
      { 
        message_start=true; 
        break;
      }  
    }
  }
  if(!message_error)
  {
    message_end=false;
    message_error=false;
    t=0;
    buffer[t++]='/';
    while (!message_end && !message_error) // find message end or error
    {
      if (Serial.available()) 
      {
        c=Serial.read(); 
        buffer[t++]=c; 
        if (t>P1_MAXBUFFER )
        {
          message_error=true;
          P1error="Max buffer";
          break;
        } 
        if (c == '!') 
        { 
          message_end=true;
          P1valid = true;
          break;
        } 
      }
    } 
    }

  if(message_error)
  {
    buffer[0]=0; *l=0;
    P1valid=false;
  }
  else
  {
    P1error="";
    P1valid=true;  
    *l=t;
  }
  digitalWrite(P1_REQUEST_PIN,LOW); //End Request!
  return (!message_error);  // 1 = ok, 0 = error
}

int getObisItemCount()
{
  int c = 0;
  OBISItem* i = p1parsed->items;
  while (i != nullptr)
  {
    c++;
    i = i->next;
  }
  return c;
}

void printObisDebug()
{
  OBISItem* i = p1parsed->items;
  while (i != nullptr)
  {
    if (i->unit != nullptr) {
      if (i->type == OBISItem::DOUBLE)
      {
        debugI("OBIS: %i-%i:%i.%i.%i Double %f %s",i->obis[0],i->obis[1],i->obis[2],i->obis[3],i->obis[4],i->value.dValue,i->unit->unitstr);
      }
      else if (i->type == OBISItem::INT32)
      {
        debugI("OBIS: %i-%i:%i.%i.%i INT32 %i %s",i->obis[0],i->obis[1],i->obis[2],i->obis[3],i->obis[4],i->value.i32Value,i->unit->unitstr);
      }
      else if (i->type == OBISItem::INT64)
      {
        debugI("OBIS: %i-%i:%i.%i.%i INT64 %i %s",i->obis[0],i->obis[1],i->obis[2],i->obis[3],i->obis[4],i->value.i64Value,i->unit->unitstr);      
      }
      else if (i->type == OBISItem::NONE) 
      {
        debugI("OBIS: %i-%i:%i.%i.%i NONE %s",i->obis[0],i->obis[1],i->obis[2],i->obis[3],i->obis[4],i->unit->unitstr);            
      }
      else if (i->type == OBISItem::CHARARR) 
      {
        debugI("OBIS: %i-%i:%i.%i.%i STR %s %s",i->obis[0],i->obis[1],i->obis[2],i->obis[3],i->obis[4],i->value.stringValue,i->unit->unitstr);            
      }    
    }
    i = i->next;
  }
}

void p1ReadAndParseNow()
{
     P1_ReadFromSerial(P1buffer,&P1length);       // load P1 buffer in Global Memory variable
     parseItems();
}


void p1loop() 
{
  if (millis() > P1NextMillis) //Should we refresh P1 data?
  {
     debugI("Reading data from serial P1 port");
     P1_ReadFromSerial(P1buffer,&P1length);       // load P1 buffer in Global Memory variable
     debugI("Parsing items...");
     parseItems();
     debugI("Length of OBIS Item list %i", getObisItemCount());
     printObisDebug();
     P1NextMillis = millis() + P1_READ_INTERVAL;
  }    
}