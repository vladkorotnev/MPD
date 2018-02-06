/*
 * Copyright (C) 2003-2015 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include "lib/upnp/ContentDirectoryService.hxx"
#include "lib/upnp/Domain.hxx"
#include "lib/upnp/ixmlwrap.hxx"
#include "lib/upnp/Action.hxx"
#include "Directory.hxx"
#include "util/NumberParser.hxx"
#include "util/UriUtil.hxx"
#include "util/Error.hxx"

#include <stdio.h>
#include <string.h>
static bool
ReadResultTag(UPnPDirContent &dirbuf, IXML_Document *response, Error &error)
{
	const char *p = ixmlwrap::getFirstElementValue(response, "Result");
	if (p == nullptr)
		p = "";

       {  /*add by lyx
            when ParentID is ""change it to "-1". The ContentDirectory spec says
            "The parentID of the Content Directory 'root' container must be set to
            the reserved value of '-1'".
         */
          const char *pParentIdStr = strstr(p,"parentID=");
          char *tempStr = NULL;
          if (pParentIdStr != NULL) {
              if(*(pParentIdStr+9)=='\"'){
                 if (*(pParentIdStr+10) == '\"') {
                    int i = 0;
                    int j = 0;
                    int len = strlen(p)+2;
                    tempStr = (char *)malloc(len);
                    while ( i < len) {
                        if (strncmp(&p[j],"parentID=",9) == 0){
                           memcpy(&tempStr[i],"parentID=\"-1\"",13);                            
			   i+=13;
                           j+=11; 
                        } else  {
                          tempStr[i] = p[j];
                          i++;
                          j++;
                        }
                    }
                   if(tempStr != NULL) {
                      dirbuf.parse(tempStr,error);
                      free(tempStr);
                      return true;
                   }
                 }
              }
          } 
         /*lyx add end  */
     }
	
        return dirbuf.parse(p, error);
}

inline bool
ContentDirectoryService::readDirSlice(UpnpClient_Handle hdl,
				      const char *objectId, unsigned offset,
				      unsigned count, UPnPDirContent &dirbuf,
				      unsigned &didreadp, unsigned &totalp,
				      Error &error) const
{
	// Create request
	char ofbuf[100], cntbuf[100];
	sprintf(ofbuf, "%u", offset);
	sprintf(cntbuf, "%u", count);
	// Some devices require an empty SortCriteria, else bad params
	IXML_Document *request =
		MakeActionHelper("Browse", m_serviceType.c_str(),
				 "ObjectID", objectId,
				 "BrowseFlag", "BrowseDirectChildren",
				 "Filter", "*",
				 "StartingIndex", ofbuf,
				 "RequestedCount", cntbuf,
                                 "SortCriteria", "");
	if (request == nullptr) {
		error.Set(upnp_domain, "UpnpMakeAction() failed");
		return false;
	}

	IXML_Document *response;
	int code = UpnpSendAction(hdl, m_actionURL.c_str(), m_serviceType.c_str(),
				  0 /*devUDN*/, request, &response);
	ixmlDocument_free(request);
	if (code != UPNP_E_SUCCESS) {
		error.Format(upnp_domain, code,
			     "UpnpSendAction() failed: %s",
			     UpnpGetErrorMessage(code));
		return false;
	}

	const char *value = ixmlwrap::getFirstElementValue(response, "NumberReturned");
	didreadp = value != nullptr
		? ParseUnsigned(value)
		: 0;

	value = ixmlwrap::getFirstElementValue(response, "TotalMatches");
	if (value != nullptr)
		totalp = ParseUnsigned(value);

	bool success = ReadResultTag(dirbuf, response, error);
	ixmlDocument_free(response);
	return success;
}

bool
ContentDirectoryService::readDir(UpnpClient_Handle handle,
				 const char *objectId,
				 UPnPDirContent &dirbuf,
				 Error &error) const
{
	unsigned offset = 0, total = -1, count;

	do {
		if (!readDirSlice(handle, objectId, offset, m_rdreqcnt, dirbuf,
				  count, total, error))
			return false;

		offset += count;
	} while (count > 0 && offset < total);

	return true;
}

bool
ContentDirectoryService::search(UpnpClient_Handle hdl,
				const char *objectId,
				const char *ss,
				UPnPDirContent &dirbuf,
				Error &error) const
{
	unsigned offset = 0, total = -1, count;

	do {
		char ofbuf[100];
		sprintf(ofbuf, "%d", offset);

		IXML_Document *request =
			MakeActionHelper("Search", m_serviceType.c_str(),
					 "ContainerID", objectId,
					 "SearchCriteria", ss,
					 "Filter", "*",					 
					 "StartingIndex", ofbuf,
					  "RequestedCount", "0",
					  "SortCriteria", ""); // Setting a value here gets twonky into fits
		if (request == 0) {
			error.Set(upnp_domain, "UpnpMakeAction() failed");
			return false;
		}

		IXML_Document *response;
		auto code = UpnpSendAction(hdl, m_actionURL.c_str(),
					   m_serviceType.c_str(),
					   0 /*devUDN*/, request, &response);
		ixmlDocument_free(request);
		if (code != UPNP_E_SUCCESS) {
			error.Format(upnp_domain, code,
				     "UpnpSendAction() failed: %s",
				     UpnpGetErrorMessage(code));
			return false;
		}

		const char *value =
			ixmlwrap::getFirstElementValue(response, "NumberReturned");
		count = value != nullptr
			? ParseUnsigned(value)
			: 0;

		offset += count;

		value = ixmlwrap::getFirstElementValue(response, "TotalMatches");
		if (value != nullptr)
			total = ParseUnsigned(value);

		bool success = ReadResultTag(dirbuf, response, error);
		ixmlDocument_free(response);
		if (!success)
			return false;
	} while (count > 0 && offset < total);

	return true;
}

bool
ContentDirectoryService::getMetadata(UpnpClient_Handle hdl,
				     const char *objectId,
				     UPnPDirContent &dirbuf,
				     Error &error) const
{
	// Create request
	IXML_Document *request =
		MakeActionHelper("Browse", m_serviceType.c_str(),
				 "ObjectID", objectId,
				 "BrowseFlag", "BrowseMetadata",
				 "Filter", "*",
				 "StartingIndex", "0",
				 "RequestedCount", "0",
                                 "SortCriteria", "");
	if (request == nullptr) {
		error.Set(upnp_domain, "UpnpMakeAction() failed");
		return false;
	}

	IXML_Document *response;
	auto code = UpnpSendAction(hdl, m_actionURL.c_str(),
				   m_serviceType.c_str(),
				   0 /*devUDN*/, request, &response);
	ixmlDocument_free(request);
	if (code != UPNP_E_SUCCESS) {
		error.Format(upnp_domain, code,
			     "UpnpSendAction() failed: %s",
			     UpnpGetErrorMessage(code));
		return false;
	}

	bool success = ReadResultTag(dirbuf, response, error);
	ixmlDocument_free(response);
	return success;
}
