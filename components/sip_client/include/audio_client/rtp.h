/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef COMPONENTS_SIP_CLIENT_INCLUDE_AUDIO_CLIENT_RTP_H_
#define COMPONENTS_SIP_CLIENT_INCLUDE_AUDIO_CLIENT_RTP_H_

#define LARGESTFRAMESIZE 1000000
#define RTP_PACKET_SIZE 1370


//#define LOG_MASK  ( ERRORS |REBUILD|DISCARD|LOG_PACKET|SKIP|FRAME)

#define LOG_MASK  (ERRORS)
#define R2(X) ( (((X) & 0xff) << 8) | ((X) >> 8) )
#define R4(X) ( ((X & 0xff) << 24) | ((X & 0xff00) << 8) | \
                ((X & 0xff0000) >> 8) | (X >> 24) )
typedef struct {
  int version :2;
  int pad :1;
  int extension :1;
  int csrccount :4;
  int marker :1;
  int payloadtype :7;
  unsigned short seq;
  unsigned int timestamp;

  unsigned int ssrc;
  unsigned int csrc;  // repeated up to 15 times

  unsigned int type :1;
  unsigned int redundant_count :3;
  unsigned int new_frame :1;
  unsigned int end_frame :1;
  unsigned int frame_type :2;

  unsigned char data[RTP_PACKET_SIZE];

  // this value doesn't actually get written or read
  unsigned int size;
} RTPPACKET;

#define RTP_PACKET_HEADER_SIZE offsetof(RTPPACKET,data)

#endif /* COMPONENTS_SIP_CLIENT_INCLUDE_AUDIO_CLIENT_RTP_H_ */
