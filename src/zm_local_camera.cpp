//
// ZoneMinder Local Camera Class Implementation, $Date$, $Revision$
// Copyright (C) 2003, 2004, 2005, 2006  Philip Coombes
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
// 

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/mman.h>

#include "zm.h"
#include "zm_local_camera.h"

int LocalCamera::camera_count = 0;
int LocalCamera::m_cap_frame = 0;
int LocalCamera::m_cap_frame_active = 0;
int LocalCamera::m_sync_frame = 0;
video_mbuf LocalCamera::m_vmb;
video_mmap *LocalCamera::m_vmm;
int LocalCamera::m_videohandle;
unsigned char *LocalCamera::m_buffer=0;

unsigned char *LocalCamera::y_table;
signed char *LocalCamera::uv_table;
short *LocalCamera::r_v_table;
short *LocalCamera::g_v_table;
short *LocalCamera::g_u_table;
short *LocalCamera::b_u_table;

LocalCamera::LocalCamera( const char *p_device, int p_channel, int p_format, int p_width, int p_height, int p_palette, int p_brightness, int p_contrast, int p_hue, int p_colour, bool p_capture ) : Camera( LOCAL, p_width, p_height, p_palette, p_brightness, p_contrast, p_hue, p_colour, p_capture ), device( p_device ), channel( p_channel ), format( p_format )
{
	if ( !camera_count++ && capture )
	{
		Initialise();
	}
}

LocalCamera::~LocalCamera()
{
	if ( !--camera_count && capture )
	{
		Terminate();
	}
}

void LocalCamera::Initialise()
{
	if ( (m_videohandle=open(device, O_RDWR)) < 0 )
	{
		Error(( "Failed to open video device %s: %s", device, strerror(errno) ));
		exit(-1);
	}

	struct video_picture vid_pic;
	memset( &vid_pic, 0, sizeof(vid_pic) );
	if ( ioctl( m_videohandle, VIDIOCGPICT, &vid_pic) < 0 )
	{
		Error(( "Failed to get picture attributes: %s", strerror(errno) ));
		exit(-1);
	}
	Debug( 1, ( "Old P:%d", vid_pic.palette ));
	Debug( 1, ( "Old D:%d", vid_pic.depth ));
	Debug( 1, ( "Old B:%d", vid_pic.brightness ));
	Debug( 1, ( "Old h:%d", vid_pic.hue ));
	Debug( 1, ( "Old Cl:%d", vid_pic.colour ));
	Debug( 1, ( "Old Cn:%d", vid_pic.contrast ));

	switch (vid_pic.palette = palette)
	{
		case VIDEO_PALETTE_GREY :
		{
			vid_pic.depth = 8;
			break;
		}
		case VIDEO_PALETTE_RGB565 :
		case VIDEO_PALETTE_YUYV :
		case VIDEO_PALETTE_YUV422 :
		case VIDEO_PALETTE_YUV420P :
		case VIDEO_PALETTE_YUV422P :
		{
			vid_pic.depth = 16;
			break;
		}
		case VIDEO_PALETTE_RGB24 :
		{
			vid_pic.depth = 24;
			break;
		}
	}

	if ( brightness >= 0 ) vid_pic.brightness = brightness;
	if ( hue >= 0 ) vid_pic.hue = hue;
	if ( colour >= 0 ) vid_pic.colour = colour;
	if ( contrast >= 0 ) vid_pic.contrast = contrast;

	if ( ioctl( m_videohandle, VIDIOCSPICT, &vid_pic ) < 0 )
	{
		Error(( "Failed to set picture attributes: %s", strerror(errno) ));
		if ( config.strict_video_config )
			exit(-1);
	}

	struct video_window vid_win;
	memset( &vid_win, 0, sizeof(vid_win) );
	if ( ioctl( m_videohandle, VIDIOCGWIN, &vid_win) < 0 )
	{
		Error(( "Failed to get window attributes: %s", strerror(errno) ));
		exit(-1);
	}
	Debug( 1, ( "Old X:%d", vid_win.x ));
	Debug( 1, ( "Old Y:%d", vid_win.y ));
	Debug( 1, ( "Old W:%d", vid_win.width ));
	Debug( 1, ( "Old H:%d", vid_win.height ));
	
	vid_win.x = 0;
	vid_win.y = 0;
	vid_win.width = width;
	vid_win.height = height;
#ifndef HAVE_V4L2
	vid_win.flags &= ~VIDEO_WINDOW_INTERLACE;
#endif

	if ( ioctl( m_videohandle, VIDIOCSWIN, &vid_win ) < 0 )
	{
		Error(( "Failed to set window attributes: %s", strerror(errno) ));
		if ( config.strict_video_config )
			exit(-1);
	}

	if ( ioctl(m_videohandle, VIDIOCGMBUF, &m_vmb) < 0 )
	{
		Error(( "Failed to setup memory: %s", strerror(errno) ));
		exit(-1);
	}
	m_vmm = new video_mmap[m_vmb.frames];
	Debug( 1, ( "vmb.frames = %d", m_vmb.frames ));
	Debug( 1, ( "vmb.size = %d", m_vmb.size ));

	for ( int i=0; i < m_vmb.frames; i++)
	{
		m_vmm[i].frame = i;
		m_vmm[i].width = width;
		m_vmm[i].height = height;
		m_vmm[i].format = palette;
	}

	m_buffer = (unsigned char *)mmap( 0, m_vmb.size, PROT_READ|PROT_WRITE, MAP_SHARED, m_videohandle, 0 );
	if ( m_buffer == MAP_FAILED )
	{
		Error(( "Could not mmap video: %s", strerror(errno) ));
		exit(-1);
	}

	struct video_channel vid_src;
	memset( &vid_src, 0, sizeof(vid_src) );
	vid_src.channel = channel;
	if ( ioctl( m_videohandle, VIDIOCGCHAN, &vid_src) < 0 )
	{
		Error(( "Failed to get camera source: %s", strerror(errno) ));
		exit(-1);
	}
	Debug( 1, ( "Old C:%d", vid_src.channel ));
	Debug( 1, ( "Old F:%d", vid_src.norm ));
	Debug( 1, ( "Old Fl:%x", vid_src.flags ));
	Debug( 1, ( "Old T:%d", vid_src.type ));

	vid_src.norm = format;
	vid_src.flags = 0;
	vid_src.type = VIDEO_TYPE_CAMERA;
	if ( ioctl( m_videohandle, VIDIOCSCHAN, &vid_src) < 0 )
	{
		Error(( "Failed to set camera source %d: %s", channel, strerror(errno) ));
		if ( config.strict_video_config )
			exit(-1);
	}

	if ( ioctl( m_videohandle, VIDIOCGWIN, &vid_win) < 0 )
	{
		Error(( "Failed to get window data: %s", strerror(errno) ));
		exit(-1);
	}
	Debug( 1, ( "New X:%d", vid_win.x ));
	Debug( 1, ( "New Y:%d", vid_win.y ));
	Debug( 1, ( "New W:%d", vid_win.width ));
	Debug( 1, ( "New H:%d", vid_win.height ));
	
	if ( ioctl( m_videohandle, VIDIOCGPICT, &vid_pic) < 0 )
	{
		Error(( "Failed to get window data: %s", strerror(errno) ));
		exit(-1);
	}
	Debug( 1, ( "New P:%d", vid_pic.palette ));
	Debug( 1, ( "New D:%d", vid_pic.depth ));
	Debug( 1, ( "New B:%d", vid_pic.brightness ));
	Debug( 1, ( "New h:%d", vid_pic.hue ));
	Debug( 1, ( "New Cl:%d", vid_pic.colour ));
	Debug( 1, ( "New Cn:%d", vid_pic.contrast ));

	y_table = new unsigned char[256];
	for ( int i = 0; i <= 255; i++ )
	{
		unsigned char c = i;
		if ( c <= 16 )
			y_table[c] = 0;
		else if ( c >= 235 )
			y_table[c] = 255;
		else
			y_table[c] = (255*(c-16))/219;
	}

	uv_table = new signed char[256];
	for ( int i = 0; i <= 255; i++ )
	{
		unsigned char c = i;
		if ( c <= 16 )
			uv_table[c] = -127;
		else if ( c >= 240 )
			uv_table[c] = 127;
		else
			uv_table[c] = (127*(c-128))/112;
	}

	r_v_table = new short[255];
	g_v_table = new short[255];
	g_u_table = new short[255];
	b_u_table = new short[255];
	r_v_table += 127;
	g_v_table += 127;
	g_u_table += 127;
	b_u_table += 127;
	for ( int i = -127; i <= 127; i++ )
	{
		signed char c = i;
		r_v_table[c] = (1402*c)/1000;
		g_u_table[c] = (344*c)/1000;
		g_v_table[c] = (714*c)/1000;
		b_u_table[c] = (1772*c)/1000;
	}
}

void LocalCamera::Terminate()
{
	munmap((char*)m_buffer, m_vmb.size);

	delete[] m_vmm;

	close(m_videohandle);
}

bool LocalCamera::GetCurrentSettings( const char *device, char *output, bool verbose )
{
	output[0] = 0;
	if ( verbose )
		sprintf( output, output+strlen(output), "Checking Video Device: %s\n", device );
	if ( (m_videohandle=open(device, O_RDWR)) <=0 )
	{
		Error(( "Failed to open video device %s: %s", device, strerror(errno) ));
		if ( verbose )
			sprintf( output+strlen(output), "Error, failed to open video device %s: %s\n", device, strerror(errno) );
		else
			sprintf( output+strlen(output), "error%d\n", errno );
		return( false );
	}

	struct video_capability vid_cap;
	if ( ioctl( m_videohandle, VIDIOCGCAP, &vid_cap ) < 0 )
	{
		Error(( "Failed to get video capabilities: %s", strerror(errno) ));
		if ( verbose )
			sprintf( output, "Error, failed to get video capabilities %s: %s\n", device, strerror(errno) );
		else
			sprintf( output, "error%d\n", errno );
		return( false );
	}
	if ( verbose )
	{
		sprintf( output+strlen(output), "Video Capabilities\n" );
		sprintf( output+strlen(output), "  Name: %s\n", vid_cap.name );
		sprintf( output+strlen(output), "  Type: %d\n%s%s%s%s%s%s%s%s%s%s%s%s%s%s", vid_cap.type,
			vid_cap.type&VID_TYPE_CAPTURE?"    Can capture\n":"",
			vid_cap.type&VID_TYPE_TUNER?"    Can tune\n":"",
			vid_cap.type&VID_TYPE_TELETEXT?"    Does teletext\n":"",
			vid_cap.type&VID_TYPE_OVERLAY?"    Overlay onto frame buffer\n":"",
			vid_cap.type&VID_TYPE_CHROMAKEY?"    Overlay by chromakey\n":"",
			vid_cap.type&VID_TYPE_CLIPPING?"    Can clip\n":"",
			vid_cap.type&VID_TYPE_FRAMERAM?"    Uses the frame buffer memory\n":"",
			vid_cap.type&VID_TYPE_SCALES?"    Scalable\n":"",
			vid_cap.type&VID_TYPE_MONOCHROME?"    Monochrome only\n":"",
			vid_cap.type&VID_TYPE_SUBCAPTURE?"    Can capture subareas of the image\n":"",
			vid_cap.type&VID_TYPE_MPEG_DECODER?"    Can decode MPEG streams\n":"",
			vid_cap.type&VID_TYPE_MPEG_ENCODER?"    Can encode MPEG streams\n":"",
			vid_cap.type&VID_TYPE_MJPEG_DECODER?"    Can decode MJPEG streams\n":"",
			vid_cap.type&VID_TYPE_MJPEG_ENCODER?"    Can encode MJPEG streams\n":""
		);
		sprintf( output+strlen(output), "  Video Channels: %d\n", vid_cap.channels );
		sprintf( output+strlen(output), "  Audio Channels: %d\n", vid_cap.audios );
		sprintf( output+strlen(output), "  Maximum Width: %d\n", vid_cap.maxwidth );
		sprintf( output+strlen(output), "  Maximum Height: %d\n", vid_cap.maxheight );
		sprintf( output+strlen(output), "  Minimum Width: %d\n", vid_cap.minwidth );
		sprintf( output+strlen(output), "  Minimum Height: %d\n", vid_cap.minheight );
	}
	else
	{
		sprintf( output+strlen(output), "N:%s,", vid_cap.name );
		sprintf( output+strlen(output), "T:%d,", vid_cap.type );
		sprintf( output+strlen(output), "nC:%d,", vid_cap.channels );
		sprintf( output+strlen(output), "nA:%d,", vid_cap.audios );
		sprintf( output+strlen(output), "mxW:%d,", vid_cap.maxwidth );
		sprintf( output+strlen(output), "mxH:%d,", vid_cap.maxheight );
		sprintf( output+strlen(output), "mnW:%d,", vid_cap.minwidth );
		sprintf( output+strlen(output), "mnH:%d,", vid_cap.minheight );
	}

	struct video_window vid_win;
	if ( ioctl( m_videohandle, VIDIOCGWIN, &vid_win ) < 0 )
	{
		Error(( "Failed to get window attributes: %s", strerror(errno) ));
		if ( verbose )
			sprintf( output, "Error, failed to get window attributes: %s\n", strerror(errno) );
		else
			sprintf( output, "error%d\n", errno );
		return( false );
	}
	if ( verbose )
	{
		sprintf( output+strlen(output), "Window Attributes\n" );
		sprintf( output+strlen(output), "  X Offset: %d\n", vid_win.x );
		sprintf( output+strlen(output), "  Y Offset: %d\n", vid_win.y );
		sprintf( output+strlen(output), "  Width: %d\n", vid_win.width );
		sprintf( output+strlen(output), "  Height: %d\n", vid_win.height );
	}
	else
	{
		sprintf( output+strlen(output), "X:%d,", vid_win.x );
		sprintf( output+strlen(output), "Y:%d,", vid_win.y );
		sprintf( output+strlen(output), "W:%d,", vid_win.width );
		sprintf( output+strlen(output), "H:%d,", vid_win.height );
	}

	struct video_picture vid_pic;
	if ( ioctl( m_videohandle, VIDIOCGPICT, &vid_pic ) < 0 )
	{
		Error(( "Failed to get picture attributes: %s", strerror(errno) ));
		if ( verbose )
			sprintf( output, "Error, failed to get picture attributes: %s\n", strerror(errno) );
		else
			sprintf( output, "error%d\n", errno );
		return( false );
	}
	if ( verbose )
	{
		sprintf( output+strlen(output), "Picture Attributes\n" );
		sprintf( output+strlen(output), "  Palette: %d - %s\n", vid_pic.palette, 
			vid_pic.palette==VIDEO_PALETTE_GREY?"Linear greyscale":(
			vid_pic.palette==VIDEO_PALETTE_HI240?"High 240 cube (BT848)":(
			vid_pic.palette==VIDEO_PALETTE_RGB565?"565 16 bit RGB":(
			vid_pic.palette==VIDEO_PALETTE_RGB24?"24bit RGB":(
			vid_pic.palette==VIDEO_PALETTE_RGB32?"32bit RGB":(
			vid_pic.palette==VIDEO_PALETTE_RGB555?"555 15bit RGB":(
			vid_pic.palette==VIDEO_PALETTE_YUV422?"YUV422 capture":(
			vid_pic.palette==VIDEO_PALETTE_YUYV?"YUYV":(
			vid_pic.palette==VIDEO_PALETTE_UYVY?"UVYV":(
			vid_pic.palette==VIDEO_PALETTE_YUV420?"YUV420":(
			vid_pic.palette==VIDEO_PALETTE_YUV411?"YUV411 capture":(
			vid_pic.palette==VIDEO_PALETTE_RAW?"RAW capture (BT848)":(
			vid_pic.palette==VIDEO_PALETTE_YUYV?"YUYV":(
			vid_pic.palette==VIDEO_PALETTE_YUV422?"YUV422":(
			vid_pic.palette==VIDEO_PALETTE_YUV422P?"YUV 4:2:2 Planar":(
			vid_pic.palette==VIDEO_PALETTE_YUV411P?"YUV 4:1:1 Planar":(
			vid_pic.palette==VIDEO_PALETTE_YUV420P?"YUV 4:2:0 Planar":(
			vid_pic.palette==VIDEO_PALETTE_YUV410P?"YUV 4:1:0 Planar":"Unknown"
		))))))))))))))))));
		sprintf( output+strlen(output), "  Colour Depth: %d\n", vid_pic.depth );
		sprintf( output+strlen(output), "  Brightness: %d\n", vid_pic.brightness );
		sprintf( output+strlen(output), "  Hue: %d\n", vid_pic.hue );
		sprintf( output+strlen(output), "  Colour :%d\n", vid_pic.colour );
		sprintf( output+strlen(output), "  Contrast: %d\n", vid_pic.contrast );
		sprintf( output+strlen(output), "  Whiteness: %d\n", vid_pic.whiteness );
	}
	else
	{
		sprintf( output+strlen(output), "P:%d,", vid_pic.palette );
		sprintf( output+strlen(output), "D:%d,", vid_pic.depth );
		sprintf( output+strlen(output), "B:%d,", vid_pic.brightness );
		sprintf( output+strlen(output), "h:%d,", vid_pic.hue );
		sprintf( output+strlen(output), "Cl:%d,", vid_pic.colour );
		sprintf( output+strlen(output), "Cn:%d,", vid_pic.contrast );
		sprintf( output+strlen(output), "w:%d,", vid_pic.whiteness );
	}

	for ( int chan = 0; chan < vid_cap.channels; chan++ )
	{
		struct video_channel vid_src;
		vid_src.channel = chan;
		if ( ioctl( m_videohandle, VIDIOCGCHAN, &vid_src ) < 0 )
		{
			Error(( "Failed to get channel %d attributes: %s", chan, strerror(errno) ));
			if ( verbose )
				sprintf( output, "Error, failed to get channel %d attributes: %s\n", chan, strerror(errno) );
			else
				sprintf( output, "error%d\n", errno );
			return( false );
		}
		if ( verbose )
		{
			sprintf( output+strlen(output), "Channel %d Attributes\n", chan );
			sprintf( output+strlen(output), "  Name: %s\n", vid_src.name );
			sprintf( output+strlen(output), "  Channel: %d\n", vid_src.channel );
			sprintf( output+strlen(output), "  Flags: %d\n%s%s", vid_src.flags,
				vid_src.flags&VIDEO_VC_TUNER?"    Channel has a tuner\n":"",
				vid_src.flags&VIDEO_VC_AUDIO?"    Channel has audio\n":""
			);
			sprintf( output+strlen(output), "  Type: %d - %s\n", vid_src.type,
				vid_src.type==VIDEO_TYPE_TV?"TV":(
				vid_src.type==VIDEO_TYPE_CAMERA?"Camera":"Unknown"
			));
			sprintf( output+strlen(output), "  Format: %d - %s\n", vid_src.norm,
				vid_src.norm==VIDEO_MODE_PAL?"PAL":(
				vid_src.norm==VIDEO_MODE_NTSC?"NTSC":(
				vid_src.norm==VIDEO_MODE_SECAM?"SECAM":(
				vid_src.norm==VIDEO_MODE_AUTO?"AUTO":"Unknown"
			))));
		}
		else
		{
			sprintf( output+strlen(output), "n%d:%s,", chan, vid_src.name );
			sprintf( output+strlen(output), "C%d:%d,", chan, vid_src.channel );
			sprintf( output+strlen(output), "Fl%d:%x,", chan, vid_src.flags );
			sprintf( output+strlen(output), "T%d:%d", chan, vid_src.type );
			sprintf( output+strlen(output), "F%d:%d%s,", chan, vid_src.norm, chan==(vid_cap.channels-1)?"":"," );
		}
	}
	return( true );
}

int LocalCamera::Brightness( int p_brightness )
{
	struct video_picture vid_pic;
	if ( ioctl( m_videohandle, VIDIOCGPICT, &vid_pic) < 0 )
	{
		Error(( "Failed to get picture attributes: %s", strerror(errno) ));
		return( -1 );
	}

	if ( p_brightness >= 0 )
	{
		vid_pic.brightness = p_brightness;
		if ( ioctl( m_videohandle, VIDIOCSPICT, &vid_pic ) < 0 )
		{
			Error(( "Failed to set picture attributes: %s", strerror(errno) ));
			return( -1 );
		}
	}
	return( vid_pic.brightness );
}

int LocalCamera::Hue( int p_hue )
{
	struct video_picture vid_pic;
	if ( ioctl( m_videohandle, VIDIOCGPICT, &vid_pic) < 0 )
	{
		Error(( "Failed to get picture attributes: %s", strerror(errno) ));
		return( -1 );
	}

	if ( p_hue >= 0 )
	{
		vid_pic.hue = p_hue;
		if ( ioctl( m_videohandle, VIDIOCSPICT, &vid_pic ) < 0 )
		{
			Error(( "Failed to set picture attributes: %s", strerror(errno) ));
			return( -1 );
		}
	}
	return( vid_pic.hue );
}

int LocalCamera::Colour( int p_colour )
{
	struct video_picture vid_pic;
	if ( ioctl( m_videohandle, VIDIOCGPICT, &vid_pic) < 0 )
	{
		Error(( "Failed to get picture attributes: %s", strerror(errno) ));
		return( -1 );
	}

	if ( p_colour >= 0 )
	{
		vid_pic.colour = p_colour;
		if ( ioctl( m_videohandle, VIDIOCSPICT, &vid_pic ) < 0 )
		{
			Error(( "Failed to set picture attributes: %s", strerror(errno) ));
			return( -1 );
		}
	}
	return( vid_pic.colour );
}

int LocalCamera::Contrast( int p_contrast )
{
	struct video_picture vid_pic;
	if ( ioctl( m_videohandle, VIDIOCGPICT, &vid_pic) < 0 )
	{
		Error(( "Failed to get picture attributes: %s", strerror(errno) ));
		return( -1 );
	}

	if ( p_contrast >= 0 )
	{
		vid_pic.contrast = p_contrast;
		if ( ioctl( m_videohandle, VIDIOCSPICT, &vid_pic ) < 0 )
		{
			Error(( "Failed to set picture attributes: %s", strerror(errno) ));
			return( -1 );
		}
	}
	return( vid_pic.contrast );
}

int LocalCamera::PreCapture()
{
	//Info(( "%s: Capturing image", id ));

	if ( camera_count > 1 )
	{
		//Info(( "Switching" ));
		struct video_channel vid_src;
		memset( &vid_src, 0, sizeof(vid_src) );
		vid_src.channel = channel;
		if ( ioctl( m_videohandle, VIDIOCGCHAN, &vid_src) < 0 )
		{
			Error(( "Failed to get camera source %d: %s", channel, strerror(errno) ));
			return(-1);
		}

		vid_src.channel = channel;
		vid_src.norm = format;
		vid_src.flags = 0;
		vid_src.type = VIDEO_TYPE_CAMERA;
		if ( ioctl( m_videohandle, VIDIOCSCHAN, &vid_src ) < 0 )
		{
			Error(( "Failed to set camera source %d: %s", channel, strerror(errno) ));
			return( -1 );
		}
	}
	m_cap_frame_active = m_cap_frame;
	if ( ioctl( m_videohandle, VIDIOCMCAPTURE, &m_vmm[m_cap_frame_active] ) < 0 )
	{
		Error(( "Capture failure for frame %d: %s", m_cap_frame_active, strerror(errno) ));
		return( -1 );
	}
	m_cap_frame = (m_cap_frame+1)%m_vmb.frames;
	return( 0 );
}

int LocalCamera::PostCapture( Image &image )
{
	//Info(( "%s: Capturing image", id ));
	int captures_per_frame = 1;
	if ( camera_count > 1 )
		captures_per_frame = config.captures_per_frame;
	while ( captures_per_frame )
	{
		if ( ioctl( m_videohandle, VIDIOCSYNC, &m_sync_frame ) < 0 )
		{
			Error(( "Sync failure for frame %d buffer %d(%d): %s", m_sync_frame, m_cap_frame_active, captures_per_frame, strerror(errno) ));
			return( -1 );
		}
		captures_per_frame--;
		if ( captures_per_frame )
		{
			if ( ioctl( m_videohandle, VIDIOCMCAPTURE, &m_vmm[m_cap_frame_active] ) < 0 )
			{
				Error(( "Capture failure for buffer %d(%d): %s", m_cap_frame_active, captures_per_frame, strerror(errno) ));
				return( -1 );
			}
		}
	}
	//Info(( "Captured %d for %d into %d", m_sync_frame, channel, m_cap_frame_active));

	unsigned char *buffer = m_buffer+(m_sync_frame*m_vmb.size/m_vmb.frames);
	m_sync_frame = (m_sync_frame+1)%m_vmb.frames;

	static unsigned char temp_buffer[ZM_MAX_IMAGE_SIZE];
	switch( palette )
	{
		case VIDEO_PALETTE_YUV420P :
		{
			static unsigned char y_plane[ZM_MAX_IMAGE_DIM];
			static char u_plane[ZM_MAX_IMAGE_DIM];
			static char v_plane[ZM_MAX_IMAGE_DIM];

			unsigned char *rgb_ptr = temp_buffer;
			unsigned char *y_ptr = y_plane;
			char *u1_ptr = u_plane;
			char *u2_ptr = u_plane+width;
			char *v1_ptr = v_plane;
			char *v2_ptr = v_plane+width;

			int Y_size = width*height;
			int C_size = Y_size>>2; // Every little bit helps...
			unsigned char *Y_ptr = buffer;
			unsigned char *Cb_ptr = buffer + Y_size;
			unsigned char *Cr_ptr = Cb_ptr + C_size;
			
			int y,u,v;
			for ( int i = 0; i < Y_size; i++ )
			{
				*y_ptr++ = y_table[*Y_ptr++];
			}
			int half_width = width>>1; // We are the king of optimisations!
			for ( int i = 0, j = 0; i < C_size; i++, j++ )
			{
				if ( j == half_width )
				{
					j = 0;
					u1_ptr += width;
					u2_ptr += width;
					v1_ptr += width;
					v2_ptr += width;
				}
				u = uv_table[*Cb_ptr++];

				*u1_ptr++ = u;
				*u1_ptr++ = u;
				*u2_ptr++ = u;
				*u2_ptr++ = u;

				v = uv_table[*Cr_ptr++];

				*v1_ptr++ = v;
				*v1_ptr++ = v;
				*v2_ptr++ = v;
				*v2_ptr++ = v;
			}

			y_ptr = y_plane;
			u1_ptr = u_plane;
			v1_ptr = v_plane;
			int size = Y_size*3;
			int r,g,b;
			for ( int i = 0; i < size; i += 3 )
			{
				y = *y_ptr++;
				u = *u1_ptr++;
				v = *v1_ptr++;

				r = y + r_v_table[v];
				g = y - (g_u_table[u]+g_v_table[v]);
				b = y + b_u_table[u];

				*rgb_ptr++ = r<0?0:(r>255?255:r);
				*rgb_ptr++ = g<0?0:(g>255?255:g);
				*rgb_ptr++ = b<0?0:(b>255?255:b);
			}
			buffer = temp_buffer;
			break;
		}
		case VIDEO_PALETTE_YUV422P :
		{
			static unsigned char y_plane[ZM_MAX_IMAGE_DIM];
			static char u_plane[ZM_MAX_IMAGE_DIM];
			static char v_plane[ZM_MAX_IMAGE_DIM];

			unsigned char *rgb_ptr = temp_buffer;
			unsigned char *y_ptr = y_plane;
			char *u1_ptr = u_plane;
			char *v1_ptr = v_plane;

			int Y_size = width*height;
			int C_size = Y_size>>1; // Every little bit helps...
			unsigned char *Y_ptr = buffer;
			unsigned char *Cb_ptr = buffer + Y_size;
			unsigned char *Cr_ptr = Cb_ptr + C_size;
			
			int y,u,v;
			for ( int i = 0; i < Y_size; i++ )
			{
				*y_ptr++ = y_table[*Y_ptr++];
			}
			for ( int i = 0, j = 0; i < C_size; i++, j++ )
			{
				u = uv_table[*Cb_ptr++];

				*u1_ptr++ = u;
				*u1_ptr++ = u;

				v = uv_table[*Cr_ptr++];

				*v1_ptr++ = v;
				*v1_ptr++ = v;
			}

			y_ptr = y_plane;
			u1_ptr = u_plane;
			v1_ptr = v_plane;
			int size = Y_size*3;
			int r,g,b;
			for ( int i = 0; i < size; i += 3 )
			{
				y = *y_ptr++;
				u = *u1_ptr++;
				v = *v1_ptr++;

				r = y + r_v_table[v];
				g = y - (g_u_table[u]+g_v_table[v]);
				b = y + b_u_table[u];

				*rgb_ptr++ = r<0?0:(r>255?255:r);
				*rgb_ptr++ = g<0?0:(g>255?255:g);
				*rgb_ptr++ = b<0?0:(b>255?255:b);
			}
			buffer = temp_buffer;
			break;
		}
		case VIDEO_PALETTE_YUYV :
		case VIDEO_PALETTE_YUV422 :
		{
			int size = width*height*2;
			unsigned char *s_ptr = buffer;
			unsigned char *d_ptr = temp_buffer;

			int y1,y2,u,v;
			int r,g,b;
			for ( int i = 0; i < size; i += 4 )
			{
				y1 = *s_ptr++;
				u = *s_ptr++;
				y2 = *s_ptr++;
				v = *s_ptr++;

				r = y1 + r_v_table[v];
				g = y1 - (g_u_table[u]+g_v_table[v]);
				b = y1 + b_u_table[u];

				*d_ptr++ = r<0?0:(r>255?255:r);
				*d_ptr++ = g<0?0:(g>255?255:g);
				*d_ptr++ = b<0?0:(b>255?255:b);

				r = y2 + r_v_table[v];
				g = y2 - (g_u_table[u]+g_v_table[v]);
				b = y2 + b_u_table[u];

				*d_ptr++ = r<0?0:(r>255?255:r);
				*d_ptr++ = g<0?0:(g>255?255:g);
				*d_ptr++ = b<0?0:(b>255?255:b);
			}
			buffer = temp_buffer;
			break;
		}
		case VIDEO_PALETTE_RGB555 :
		{
			int size = width*height*2;
			unsigned char r,g,b;
			unsigned char *s_ptr = buffer;
			unsigned char *d_ptr = temp_buffer;
			for ( int i = 0; i < size; i += 2 )
			{
				b = ((*s_ptr)<<3)&0xf8;
				g = (((*(s_ptr+1))<<6)|((*s_ptr)>>2))&0xf8;
				r = ((*(s_ptr+1))<<1)&0xf8;

				*d_ptr++ = r;
				*d_ptr++ = g;
				*d_ptr++ = b;
				s_ptr += 2;
			}
			buffer = temp_buffer;
			break;
		}
		case VIDEO_PALETTE_RGB565 :
		{
			int size = width*height*2;
			unsigned char r,g,b;
			unsigned char *s_ptr = buffer;
			unsigned char *d_ptr = temp_buffer;
			for ( int i = 0; i < size; i += 2 )
			{
				b = ((*s_ptr)<<3)&0xf8;
				g = (((*(s_ptr+1))<<5)|((*s_ptr)>>3))&0xfc;
				r = (*(s_ptr+1))&0xf8;

				*d_ptr++ = r;
				*d_ptr++ = g;
				*d_ptr++ = b;
				s_ptr += 2;
			}
			buffer = temp_buffer;
			break;
		}
		case VIDEO_PALETTE_RGB24 :
		{
			if ( config.local_bgr_invert )
			{
				int size = width*height*3;
				unsigned char *s_ptr = buffer;
				unsigned char *d_ptr = temp_buffer;
				for ( int i = 0; i < size; i += 3 )
				{
					*d_ptr++ = *(s_ptr+2);
					*d_ptr++ = *(s_ptr+1);
					*d_ptr++ = *s_ptr;
					s_ptr += 3;
				}
				buffer = temp_buffer;
			}
			break;
		}
		case VIDEO_PALETTE_GREY :
		{
			//int size = width*height;
			//for ( int i = 0; i < size; i++ )
			//{
				//if ( buffer[i] < 16 )
					//Info(( "Lo grey %d", buffer[i] ));
				//if ( buffer[i] > 235 )
					//Info(( "Hi grey %d", buffer[i] ));
			//}
		}
		default : // Everything else is straightforward, for now.
		{
			break;
		}
	}

	image.Assign( width, height, colours, buffer );

	return( 0 );
}
