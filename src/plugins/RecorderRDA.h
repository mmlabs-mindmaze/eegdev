#ifndef _INC_RECORDERRDA
#define _INC_RECORDERRDA
// MODULE: RDA.h
//: written by: Henning Nordholz
//+       date: 14-Nov-00
//+ 
//+ Description:
//+ 	Vision Recorder 
//. 		Remote Data Access (RDA) structs and constants

#include <stdint.h>

#pragma pack(1)
#ifndef ULONG
typedef uint32_t ULONG;
#endif

#ifndef GUID
typedef unsigned char GUID[16];
#endif

//#define GUID_RDAHeader {0x4358458e, 0xc996, 0x4c86, 0xaf, 0x4a, 0x98, 0xbb, 0xf6, 0xc9, 0x14, 0x50}
const GUID GUID_RDAHeader = {0x4358458e, 0xc996, 0x4c86, 0xaf, 0x4a, 0x98, 0xbb, 0xf6, 0xc9, 0x14, 0x50};

// All numbers are sent in little endian format.

// Unique identifier for messages sent to clients
// {4358458E-C996-4C86-AF4A-98BBF6C91450}
// As byte array (16 bytes): 8E45584396C9864CAF4A98BBF6C91450
//DEFINE_GUID(GUID_RDAHeader,
//0x4358458e, 0xc996, 0x4c86, 0xaf, 0x4a, 0x98, 0xbb, 0xf6, 0xc9, 0x14, 0x50);

typedef struct
//; A single marker in the marker array of RDA_MessageData
{
	ULONG				nSize;				// Size of this marker.
	ULONG				nPosition;			// Relative position in the data block.
	ULONG				nPoints;			// Number of points of this marker
	int32_t				nChannel;			// Associated channel number (-1 = all channels).
	char				sTypeDesc[1];			// Type, description in ASCII delimited by '\0'.
} RDA_Marker;


typedef struct
//; Message header
{
	GUID guid;		// Always GUID_RDAHeader
	ULONG nSize; 		// Size of the message block in bytes including this header
	ULONG nType;		// Message type.
} RDA_MessageHeader;


// **** Messages sent by the RDA server to the clients. ****
typedef struct
//; Setup / Start infos, Header -> nType = 1
{
	RDA_MessageHeader		header;			//Trying to replicate struct inheritance
	ULONG				nChannels;		// Number of channels
	double				dSamplingInterval;	// Sampling interval in microseconds
	double				dResolutions[1];	// Array of channel resolutions -> double dResolutions[nChannels]
								// coded in microvolts. i.e. RealValue = resolution * A/D value
	char 				sChannelNames[1];	// Channel names delimited by '\0'. The real size is 
											// larger than 1.
} RDA_MessageStart;


typedef struct
//; Block of 16-bit data, Header -> nType = 2, sent only from port 51234
{
	RDA_MessageHeader		header;				//Trying to replicate struct inheritance
	ULONG				nBlock;				// Block number, i.e. acquired blocks since acquisition started.
	ULONG				nPoints;			// Number of data points in this block
	ULONG				nMarkers;			// Number of markers in this data block
	short				nData[1];			// Data array -> short nData[nChannels * nPoints], multiplexed
	RDA_Marker			Markers[1];			// Array of markers -> RDA_Marker Markers[nMarkers]
} RDA_MessageData;

typedef struct
//; Data acquisition has been stopped. // Header -> nType = 3
{
	RDA_MessageHeader		header;			//Trying to replicate struct inheritance
} RDA_MessageStop;

typedef struct
//; Block of 32-bit floating point data, Header -> nType = 4, sent only from port 51244
{
	RDA_MessageHeader		header;			//Trying to replicate struct inheritance
	ULONG				nBlock;				// Block number, i.e. acquired blocks since acquisition started.
	ULONG				nPoints;			// Number of data points in this block
	ULONG				nMarkers;			// Number of markers in this data block
	float				fData[1];			// Data array -> float fData[nChannels * nPoints], multiplexed
	RDA_Marker			Markers[1];			// Array of markers -> RDA_Marker Markers[nMarkers]
} RDA_MessageData32;
#pragma pack()
// **** End Messages sent by the RDA server to the clients. ****


#endif //_INC_RECORDERRDA
