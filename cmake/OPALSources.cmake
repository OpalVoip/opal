# Source list transcribed from make/toplevel.mak.

set(OPAL_SOURCES
  src/opal/manager.cxx
  src/opal/endpoint.cxx
  src/opal/connection.cxx
  src/opal/call.cxx
  src/opal/mediafmt.cxx
  src/opal/mediatype.cxx
  src/opal/mediasession.cxx
  src/opal/mediastrm.cxx
  src/opal/patch.cxx
  src/opal/transcoders.cxx
  src/codec/resample.cpp
  src/opal/transports.cxx
  src/opal/guid.cxx
  src/rtp/rtp.cxx
  src/rtp/rtp_session.cxx
  src/rtp/rtp_stream.cxx
  src/rtp/rtp_fec.cxx
  src/rtp/jitter.cxx
  src/rtp/metrics.cxx
  src/rtp/pcapfile.cxx
  src/rtp/rtpep.cxx
  src/rtp/rtpconn.cxx
  src/ep/localep.cxx
  src/ep/sockep.cxx
  src/opal/opal_c.cxx
  src/opal/pres_ent.cxx
  src/im/t140.cxx
  src/im/im_mf.cxx
  src/im/rfc4103.cxx
  src/codec/g711codec.cxx
  src/codec/g711.c
  src/codec/g722mf.cxx
  src/codec/g7221mf.cxx
  src/codec/g7222mf.cxx
  src/codec/g7231mf.cxx
  src/codec/g726mf.cxx
  src/codec/g728mf.cxx
  src/codec/g729mf.cxx
  src/codec/gsm0610mf.cxx
  src/codec/gsmamrmf.cxx
  src/codec/iLBCmf.cxx
  src/codec/speexmf.cxx
  src/codec/opusmf.cxx
  src/t38/t38mf.cxx
  src/codec/rfc2833.cxx
  src/codec/opalwavfile.cxx
  src/codec/silencedetect.cxx
  src/codec/opalpluginmgr.cxx
)

if(OPAL_HAVE_PTLIB_CLI)
  list(APPEND OPAL_SOURCES src/opal/console_mgr.cxx)
endif()
if(OPAL_IVR)
  list(APPEND OPAL_SOURCES src/ep/ivr.cxx src/ep/opalvxml.cxx)
endif()
if(OPAL_HAS_MIXER)
  list(APPEND OPAL_SOURCES
    src/opal/recording.cxx
    src/ep/opalmixer.cxx
    src/ep/opalconf.cxx)
endif()
if(OPAL_HAS_PCSS)
  list(APPEND OPAL_SOURCES src/ep/pcss.cxx)
endif()
if(OPAL_HAVE_PTLIB_GSTREAMER)
  list(APPEND OPAL_SOURCES src/ep/GstEndPoint.cxx)
  opal_define(OPAL_GSTREAMER 1)
endif()

if(OPAL_H323)
  list(APPEND OPAL_SOURCES
    src/asn/x880.cxx
    src/asn/mcs.cxx
    src/asn/gcc.cxx
    src/asn/h225_1.cxx
    src/asn/h225_2.cxx
    src/asn/h235.cxx
    src/asn/h245_1.cxx
    src/asn/h245_2.cxx
    src/asn/h245_3.cxx
    src/asn/h248.cxx
    src/h323/h323ep.cxx
    src/h323/h323.cxx
    src/h323/h323caps.cxx
    src/h323/h323neg.cxx
    src/h323/h323pdu.cxx
    src/h323/h323rtp.cxx
    src/h323/channels.cxx
    src/h323/svcctrl.cxx
    src/h323/q931.cxx
    src/h323/transaddr.cxx
    src/h323/gkclient.cxx
    src/h323/gkserver.cxx
    src/h323/h225ras.cxx
    src/h323/h323trans.cxx
    src/h323/h235auth.cxx)
  if(OPAL_H450)
    list(APPEND OPAL_SOURCES
      src/asn/h4501.cxx src/asn/h4502.cxx src/asn/h4503.cxx src/asn/h4504.cxx
      src/asn/h4505.cxx src/asn/h4506.cxx src/asn/h4507.cxx src/asn/h4508.cxx
      src/asn/h4509.cxx src/asn/h45010.cxx src/asn/h45011.cxx
      src/h323/h450pdu.cxx)
  endif()
  if(OPAL_HAVE_PTLIB_SSL)
    list(APPEND OPAL_SOURCES src/h323/h235auth1.cxx)
  endif()
  if(OPAL_H501)
    list(APPEND OPAL_SOURCES
      src/asn/h501.cxx
      src/h323/h501pdu.cxx
      src/h323/h323annexg.cxx
      src/h323/peclient.cxx)
  endif()
  if(OPAL_H460)
    list(APPEND OPAL_SOURCES
      src/h460/h4601.cxx
      src/h460/h46018.cxx
      src/h460/h46019.cxx
      src/h460/h4609.cxx
      src/h460/h460p.cxx
      src/h460/h460pres.cxx
      src/h460/h460tm.cxx
      src/h460/h46024b.cxx
      src/h460/h460_std18.cxx
      src/h460/h460_std19.cxx
      src/h460/h460_std23.cxx
      src/h460/h460_std24.cxx)
  endif()
  if(OPAL_T120DATA)
    list(APPEND OPAL_SOURCES
      src/t120/t120proto.cxx
      src/t120/h323t120.cxx
      src/t120/x224.cxx)
  endif()
  if(OPAL_T38_CAPABILITY)
    list(APPEND OPAL_SOURCES src/t38/h323t38.cxx)
  endif()
  if(OPAL_HAS_H224)
    list(APPEND OPAL_SOURCES src/h224/h323h224.cxx)
  endif()
  if(OPAL_H235_6)
    list(APPEND OPAL_SOURCES
      src/h323/h235dh.cxx
      src/rtp/h235_session.cxx)
  endif()
  if(OPAL_SRTP_ENABLED AND OPAL_H235_8)
    list(APPEND OPAL_SOURCES src/asn/h235_srtp.cxx)
  endif()
endif()

if(OPAL_SDP)
  list(APPEND OPAL_SOURCES
    src/sdp/sdpep.cxx
    src/sdp/sdphttpep.cxx
    src/sdp/sdp.cxx
    src/sdp/ice.cxx)
  if(OPAL_T38_CAPABILITY)
    list(APPEND OPAL_SOURCES src/t38/sipt38.cxx)
  endif()
endif()

if(OPAL_SIP)
  list(APPEND OPAL_SOURCES
    src/sip/sipep.cxx
    src/sip/sipcon.cxx
    src/sip/sippdu.cxx
    src/sip/handlers.cxx
    src/sip/sippres.cxx)
endif()

if(OPAL_SKINNY)
  list(APPEND OPAL_SOURCES src/ep/skinnyep.cxx)
endif()
if(OPAL_LID)
  list(APPEND OPAL_SOURCES
    src/lids/lid.cxx
    src/lids/lidep.cxx
    src/lids/lidpluginmgr.cxx)
endif()
if(OPAL_CAPI)
  list(APPEND OPAL_SOURCES src/lids/capi_ep.cxx)
endif()
if(OPAL_DAHDI)
  list(APPEND OPAL_SOURCES src/lids/dahdi_ep.cxx)
endif()

if(OPAL_SRTP_ENABLED)
  list(APPEND OPAL_SOURCES
    src/rtp/srtp_session.cxx
    src/rtp/dtls_srtp_session.cpp)
endif()

if(OPAL_HAS_MSRP)
  list(APPEND OPAL_SOURCES src/im/msrp.cxx)
endif()
if(OPAL_HAS_SIPIM)
  list(APPEND OPAL_SOURCES src/im/sipim.cxx)
endif()

if(OPAL_VIDEO)
  list(APPEND OPAL_SOURCES
    src/codec/vidcodec.cxx
    src/codec/h261mf.cxx
    src/codec/h263mf.cxx
    src/codec/h264mf.cxx
    src/codec/h265mf.cxx
    src/codec/vp8mf.cxx
    src/codec/mpeg4mf.cxx)
endif()
if(OPAL_RFC4175)
  list(APPEND OPAL_SOURCES src/codec/rfc4175.cxx)
endif()
if(OPAL_RFC2435)
  list(APPEND OPAL_SOURCES src/codec/rfc2435.cxx)
endif()
if(OPAL_G711PLC)
  list(APPEND OPAL_SOURCES src/codec/g711a1_plc.cxx)
endif()

if(OPAL_AEC AND OPAL_AEC_BUNDLED)
  list(APPEND OPAL_SOURCES
    src/codec/echocancel.cxx
    src/codec/speex/libspeex/speex_preprocess.c
    src/codec/speex/libspeex/smallft.c
    src/codec/speex/libspeex/misc.c
    src/codec/speex/libspeex/mdf.c
    src/codec/speex/libspeex/math_approx.c
    src/codec/speex/libspeex/kiss_fftr.c
    src/codec/speex/libspeex/kiss_fft.c
    src/codec/speex/libspeex/fftwrap.c)
elseif(OPAL_AEC)
  list(APPEND OPAL_SOURCES src/codec/echocancel.cxx)
endif()

if(OPAL_HAS_H224)
  list(APPEND OPAL_SOURCES
    src/h224/q922.cxx
    src/h224/h224.cxx
    src/h224/h281.cxx)
endif()
if(OPAL_FAX)
  list(APPEND OPAL_SOURCES src/t38/t38proto.cxx src/asn/t38.cxx)
endif()

if(OPAL_BFCP)
  set(_opal_bfcp_sources
    src/sdp/BFCP/libbfcp/BFCPconnection.cpp
    src/sdp/BFCP/libbfcp/BFCP_fsm.cpp
    src/sdp/BFCP/libbfcp/bfcpsrvctl/bfcpsrv/bfcp_user_list.cpp
    src/sdp/BFCP/libbfcp/bfcpsrvctl/bfcpsrv/bfcp_server.cpp
    src/sdp/BFCP/libbfcp/bfcpsrvctl/bfcpsrv/bfcp_link_list.cpp
    src/sdp/BFCP/libbfcp/bfcpsrvctl/bfcpsrv/bfcp_floor_list.c
    src/sdp/BFCP/libbfcp/bfcpsrvctl/bfcpcli/bfcp_participant.cpp
    src/sdp/BFCP/libbfcp/bfcpmsg/bfcp_strings.c
    src/sdp/BFCP/libbfcp/bfcpmsg/bfcp_messages_parse.c
    src/sdp/BFCP/libbfcp/bfcpmsg/bfcp_messages_build.c
    src/sdp/BFCP/libbfcp/bfcpmsg/bfcp_messages.c)
  list(APPEND OPAL_SOURCES src/sdp/bfcpinterface.cxx ${_opal_bfcp_sources})
endif()

list(TRANSFORM OPAL_SOURCES PREPEND "${OPAL_SOURCE_DIR}/")

# Vendored BFCP checks WIN32. MinGW defines _WIN32 and leaves WIN32 unset,
# so the Unix headers (arpa/inet.h) are selected.
if(OPAL_BFCP AND WIN32)
  set(_opal_bfcp_abs "")
  foreach(_src IN LISTS _opal_bfcp_sources)
    list(APPEND _opal_bfcp_abs "${OPAL_SOURCE_DIR}/${_src}")
  endforeach()
  set_source_files_properties(${_opal_bfcp_abs} PROPERTIES COMPILE_DEFINITIONS "WIN32")
endif()
