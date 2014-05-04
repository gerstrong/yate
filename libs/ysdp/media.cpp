/**
 * media.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * SDP media handling
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <yatesdp.h>
#include <yateice.h>

namespace TelEngine {

/*
 * SDPMedia
 */
SDPMedia::SDPMedia(const char* media, const char* transport, const char* formats,
    int rport, int lport)
    : String(media),
    m_audio(true), m_video(false), m_modified(false), m_securable(true),
    m_localChanged(false),
    m_transport(transport), m_formats(formats),
    m_rfc2833(String::boolText(false)),
    m_lAttrs(media), m_rAttrs(media), m_fmtps(media),
    m_lIceCandidates(NULL), m_rIceCandidates(NULL)
{
    DDebug(DebugAll,"SDPMedia::SDPMedia('%s','%s','%s',%d,%d) [%p]",
	media,transport,formats,rport,lport,this);
    if (String::operator!=(YSTRING("audio"))) {
	m_audio = false;
	m_video = String::operator==(YSTRING("video"));
	m_suffix << "_" << media;
    }
    int q = m_formats.find(',');
    m_format = m_formats.substr(0,q);
    if (rport >= 0)
	m_rPort = rport;
    if (lport >= 0)
	m_lPort = lport;
}

SDPMedia::~SDPMedia()
{
    DDebug(DebugAll,"SDPMedia::~SDPMedia() '%s' [%p]",c_str(),this);
    delete m_lIceCandidates;
}

const char* SDPMedia::fmtList() const
{
    if (m_formats)
	return m_formats.c_str();
    if (m_format)
	return m_format.c_str();
    // unspecified audio assumed to support G711
    if (m_audio)
	return "alaw,mulaw";
    return 0;
}

// Update members with data taken from a SDP, return true if something changed
bool SDPMedia::update(const char* formats, int rport, int lport, bool force)
{
    DDebug(DebugAll,"SDPMedia::update('%s',%d,%d,%s) [%p]",
	formats,rport,lport,String::boolText(force),this);
    bool chg = false;
    String tmp(formats);
    if (tmp && (m_formats != tmp)) {
	if (tmp.find(',') < 0) {
	    // single format received, check if acceptable
	    if (m_formats && !force && m_formats.find(tmp) < 0) {
		Debug(DebugNote,"Not changing to '%s' from '%s' [%p]",
		    formats,m_formats.c_str(),this);
		tmp.clear();
	    }
	}
	else if (m_formats && !force) {
	    // from received list keep only already offered formats
	    ObjList* l1 = tmp.split(',',false);
	    ObjList* l2 = m_formats.split(',',false);
	    for (ObjList* fmt = l1->skipNull(); fmt; ) {
		if (l2->find(fmt->get()->toString()))
		    fmt = fmt->skipNext();
		else {
		    fmt->remove();
		    fmt = fmt->skipNull();
		}
	    }
	    tmp.clear();
	    tmp.append(l1,",");
	    TelEngine::destruct(l1);
	    TelEngine::destruct(l2);
	    if (tmp.null())
		Debug(DebugNote,"Not changing formats '%s' [%p]",m_formats.c_str(),this);
	}
	if (tmp && (m_formats != tmp)) {
	    chg = true;
	    m_formats = tmp;
	    int q = m_formats.find(',');
	    m_format = m_formats.substr(0,q);
	    Debug(DebugInfo,"Choosing offered '%s' format '%s' [%p]",
		c_str(),m_format.c_str(),this);
	}
    }
    if (rport >= 0) {
	tmp = rport;
	if (m_rPort != tmp) {
	    chg = true;
	    m_rPort = tmp;
	}
    }
    if (lport >= 0) {
	tmp = lport;
	if (m_lPort != tmp) {
	    m_localChanged = true;
	    chg = true;
	    m_lPort = tmp;
	}
    }
    return chg;
}

// Update members from a dispatched "chan.rtp" message
void SDPMedia::update(const NamedList& msg, bool pickFormat)
{
    DDebug(DebugAll,"SDPMedia::update('%s',%s) [%p]",
	msg.c_str(),String::boolText(pickFormat),this);
    m_id = msg.getValue("rtpid",m_id);
    m_lPort = msg.getValue("localport",m_lPort);
    if (pickFormat) {
	const char* format = msg.getValue("format");
	if (format) {
	    m_format = format;
	    if ((m_formats != m_format) && (msg.getIntValue("remoteport") > 0)) {
		Debug(DebugNote,"Choosing started '%s' format '%s' [%p]",
		    c_str(),format,this);
		m_formats = m_format;
	    }
	}
    }
    IceRtpCandidate* c = NULL;
    if(!m_lIceCandidates) {
	m_lIceCandidates = new IceRtpCandidates();
	m_lIceCandidates->generateIceAuth();
	c = new IceRtpCandidate(m_id + "_candidate_" + String((int)Random::random()));
	c->m_component = 1; // rtp
	c->m_generation = 666;
	c->m_priority = 10;
	c->m_protocol = "udp";
	c->m_type = "host";
	m_lIceCandidates->append(c);
    }
    else
	c = m_lIceCandidates->findByComponent(1);
    c->m_address = msg.getValue("localip");
    c->m_port = m_lPort;
}

// Add or replace a parameter by name and value, set the modified flag
void SDPMedia::parameter(bool remote, const char* name, const char* value, bool append)
{
    if (!name)
	return;
    NamedList& nl = remote ? m_rAttrs : m_lAttrs;
    m_modified = true;
    if (append)
	nl.addParam(name,value);
    else
	nl.setParam(name,value);
}

// Add or replace a parameter, set the modified flag
void SDPMedia::parameter(bool remote, NamedString* param, bool append)
{
    if (!param)
	return;
    NamedList& nl = remote ? m_rAttrs : m_lAttrs;
    m_modified = true;
    if (append)
	nl.addParam(param);
    else
	nl.setParam(param);
}

// Set or reset format parameter
void SDPMedia::fmtp(const char* format, const char* parameter)
{
    if(parameter)
	m_fmtps.setParam(format, parameter);
    else
	m_fmtps.clearParam(format);
    m_modified = true;
}

// Set format parameter
void SDPMedia::fmtp(NamedString* parameter)
{
    if(! parameter)
	return;
    m_fmtps.setParam(parameter);
    m_modified = true;
}

void SDPMedia::crypto(const char* desc, bool remote)
{
    String& sdes = remote ? m_rCrypto : m_lCrypto;
    if (sdes != desc) {
	sdes = desc;
	m_modified = true;
    }
    if (remote && !desc)
	m_securable = false;
}

// Append all paramters from one named list to another, inserting keys prefix
static void putNamedList(NamedList& dst, const NamedList& src, String prefix)
{
    unsigned int n = src.length();
    for (unsigned int i = 0; i < n; i++) {
	const NamedString* param = src.getParam(i);
	if (param)
	    dst.addParam(prefix + param->name(),*param);
    }
}

// Put the list of net media in a parameter list
void SDPMedia::putMedia(NamedList& msg, bool putPort)
{
    msg.addParam("media" + suffix(),"yes");
    msg.addParam("formats" + suffix(),formats());
    msg.addParam("transport" + suffix(),transport());
    if (mappings())
	msg.addParam("rtp_mapping" + suffix(),mappings());
    if (isAudio())
	msg.addParam("rtp_rfc2833",rfc2833());
    if (putPort)
	msg.addParam("rtp_port" + suffix(),remotePort());
    if (remoteCrypto())
	msg.addParam("crypto" + suffix(),remoteCrypto());
    // must handle encryption differently
    const char* enc = m_rAttrs.getValue("encryption");
    if (enc)
	msg.addParam("encryption" + suffix(),enc);
    putNamedList(msg, m_rAttrs, "sdp" + suffix() + "_");
    putNamedList(msg, m_fmtps, "fmtp_");
}

void SDPMedia::ice(IceRtpCandidates* c, bool remote)
{
    IceRtpCandidates*& m = remote ? m_rIceCandidates : m_lIceCandidates;
    delete m;
    m = c;
}

};   // namespace TelEngine

/* vi: set ts=8 sw=4 sts=4 noet: */
