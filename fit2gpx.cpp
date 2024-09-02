#include <stdio.h>
#include <stdint.h>

#include <vector>
#include <string>
#include <chrono>
#include <print>

// include FitSDK files into single translation unit
#define FIT_USE_STDINT_H
#include "FitSDK/c/fit.c"
#include "FitSDK/c/fit_convert.c"
#include "FitSDK/c/fit_crc.c"
#include "FitSDK/c/fit_example.c"

std::vector<uint8_t> read_file( char const * filename )
{
	std::vector<uint8_t> buf;

	FILE * f = fopen( filename, "rb" );
	if( f )
	{
		fseek( f, 0, SEEK_END );
		size_t const sz = ftell( f );
		buf.resize( sz );
		fseek( f, 0, SEEK_SET );
		fread( buf.data(), 1, sz, f );
		fclose( f );
	}

	return buf;
}

void fit2gpx( char const * fit_filename )
{
	std::vector<uint8_t> fit_file = read_file( fit_filename );

	std::string gpx_filename = fit_filename;
	size_t last_dot = gpx_filename.find_last_of( '.' );
	if( last_dot != std::string::npos )
		gpx_filename.resize( last_dot );
	gpx_filename += ".gpx";

	FILE * f = fopen( gpx_filename.c_str(), "wb" );
	std::print( f, 
R"""(<?xml version="1.0" encoding="UTF-8"?>
<gpx xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd http://www.garmin.com/xmlschemas/GpxExtensions/v3 http://www.garmin.com/xmlschemas/GpxExtensionsv3.xsd http://www.garmin.com/xmlschemas/TrackPointExtension/v1 http://www.garmin.com/xmlschemas/TrackPointExtensionv1.xsd" creator="StravaGPX" version="1.1" xmlns="http://www.topografix.com/GPX/1/1" xmlns:gpxtpx="http://www.garmin.com/xmlschemas/TrackPointExtension/v1" xmlns:gpxx="http://www.garmin.com/xmlschemas/GpxExtensions/v3">
<trk>
)""" );

	FitConvert_Init( FIT_TRUE );

	uint8_t const * fit_file_data = fit_file.data();
	uint32_t fit_sz = uint32_t(fit_file.size());
	for( ; ; )
	{
		auto fcr = FitConvert_Read( fit_file_data, fit_sz );
		if( fcr == FIT_CONVERT_MESSAGE_AVAILABLE )
		{
			uint8_t const * msg = FitConvert_GetMessageData();
			auto msg_num = FitConvert_GetMessageNumber();
			switch( msg_num )
			{
			case FIT_MESG_NUM_RECORD:
				{
					auto record = (FIT_RECORD_MESG const *)msg;
					constexpr double semicircles_to_deg = 180./double(0x80000000);
					double lat = double(record->position_lat)*semicircles_to_deg, lon = double(record->position_long)*semicircles_to_deg;
					std::print( f, "<trkpt lat=\"{}\" lon=\"{}\">", lat, lon );
					if( record->altitude != FIT_UINT16_INVALID )					
					{
						float ele = (record->altitude)/5.f - 500.f;
						std::print( f, "<ele>{:.1f}</ele>", ele );
					}
					if( record->timestamp != FIT_DATE_TIME_INVALID )
					{
						using namespace std::chrono;
						constexpr auto base = sys_days( 1989y / December / 31d );
						auto timestamp = base + seconds( record->timestamp );
						std::print( f, "<time>{:%FT%H:%M:%SZ}</time>\n", timestamp );
					}
					if( record->temperature != FIT_SINT8_INVALID || record->heart_rate != FIT_UINT8_INVALID )
					{
						std::print( f, "<extensions><gpxtpx:TrackPointExtension>" );
						if( record->temperature != FIT_SINT8_INVALID )
							std::print( f, "<gpxtpx:atemp>{}</gpxtpx:atemp>", record->temperature );
						if( record->heart_rate != FIT_UINT8_INVALID )
							std::print( f, "<gpxtpx:hr>{}</gpxtpx:hr>", record->heart_rate );
						std::print( f, "</gpxtpx:TrackPointExtension></extensions>" );
					}
					std::print( f, "</trkpt>\n" );
				}
				break;
			case FIT_MESG_NUM_EVENT:
				{
					auto e = (FIT_EVENT_MESG const *)msg;
					if( e->event == FIT_EVENT_TIMER )
					{
						switch( e->event_type )
						{
						case FIT_EVENT_TYPE_START:
							std::print( f, "<trkseg>\n" );
							break;
						case FIT_EVENT_TYPE_STOP:
						case FIT_EVENT_TYPE_STOP_ALL:
							std::print( f, "</trkseg>\n" );
							break;
						}
					}
				}
				break;
			}
		}
		else
		{
			switch( fcr )
			{
			case FIT_CONVERT_CONTINUE:
				std::print( "Incomplete file: {}\n", fit_filename );
				break;
			case FIT_CONVERT_ERROR:
				std::print( "Error while parsing file: {}\n", fit_filename );
				break;
			case FIT_CONVERT_DATA_TYPE_NOT_SUPPORTED:
				std::print( "Unsupported data in file: {}\n", fit_filename );
				break;
			case FIT_CONVERT_PROTOCOL_VERSION_NOT_SUPPORTED:
				std::print( "Unsupported protocol version in file: {}\n", fit_filename );
				break;
			case FIT_CONVERT_END_OF_FILE:
				std::print( "OK: {}\n", fit_filename );
				break;
			}
			goto finish;
		}
	}

finish:
	std::print( f,
		R"""(</trk>
</gpx>
)""" );

	fclose( f );
}

int main( int argc, char const * argv[] )
{
	while( --argc )
		fit2gpx( *(++argv) );
}