#include <algorithm>
#include <iostream>
#include <string>
#include "TeleBoy.h"
#include <sstream>
#include "p8-platform/sockets/tcp.h"
#include <map>
#include <time.h>
#include <random>
#include "Utils.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#pragma comment(lib, "ws2_32.lib")
#include <stdio.h>
#include <stdlib.h>
#define timegm _mkgmtime
#endif

#ifdef TARGET_ANDROID
#include "to_string.h"
#endif

using namespace ADDON;
using namespace std;
using namespace rapidjson;

static const string tbUrl = "https://www.teleboy.ch";
static const string apiUrl = "http://tv.api.teleboy.ch";
static const string apiKey =
    "69d4547562510efe1b5d354bc34656fb34366b6c1023739ce46958007bf17ee9";
static const string apiDeviceType = "desktop";
static const string apiVersion = "1.5";

string TeleBoy::HttpGet(Curl &curl, string url)
{
  return HttpRequest(curl, "GET", url, "");
}

string TeleBoy::HttpDelete(Curl &curl, string url)
{
  return HttpRequest(curl, "DELETE", url, "");
}

string TeleBoy::HttpPost(Curl &curl, string url, string postData)
{
  return HttpRequest(curl, "POST", url, postData);
}

string TeleBoy::HttpRequest(Curl &curl, string action, string url,
    string postData)
{
  int statusCode;
  XBMC->Log(LOG_DEBUG, "Http-Request: %s %s.", action.c_str(), url.c_str());
  string content;
  if (action.compare("POST") == 0)
  {
    content = curl.Post(url, postData, statusCode);
  }
  else if (action.compare("DELETE") == 0)
  {
    content = curl.Delete(url, statusCode);
  }
  else
  {
    content = curl.Get(url, statusCode);
  }
  string cinergys = curl.GetCookie("cinergy_s");
  if (!cinergys.empty())
  {
    cinergySCookies = cinergys;
  }
  return content;
}

void TeleBoy::ApiSetHeader(Curl &curl)
{
  curl.AddHeader("x-teleboy-apikey", apiKey);
  curl.AddHeader("x-teleboy-device-type", apiDeviceType);
  curl.AddHeader("x-teleboy-session", cinergySCookies);
  curl.AddHeader("x-teleboy-version", apiVersion);
}

bool TeleBoy::ApiGetResult(string content, Document &doc)
{
  doc.Parse(content.c_str());
  if (!doc.GetParseError())
  {
    if (doc["success"].GetBool())
    {
      return true;
    }
  }
  return false;
}

bool TeleBoy::ApiGet(string url, Document &doc)
{
  Curl curl;
  ApiSetHeader(curl);
  string content = HttpGet(curl, apiUrl + url);
  curl.ResetHeaders();
  return ApiGetResult(content, doc);
}

bool TeleBoy::ApiPost(string url, string postData, Document &doc)
{
  Curl curl;
  ApiSetHeader(curl);
  if (!postData.empty())
  {
    curl.AddHeader("Content-Type", "application/json");
  }
  string content = HttpPost(curl, apiUrl + url, postData);
  curl.ResetHeaders();
  return ApiGetResult(content, doc);
}

bool TeleBoy::ApiDelete(string url, Document &doc)
{
  Curl curl;
  ApiSetHeader(curl);
  string content = HttpDelete(curl, apiUrl + url);
  curl.ResetHeaders();
  return ApiGetResult(content, doc);
}

TeleBoy::TeleBoy(bool favoritesOnly) :
    username(""), password(""), maxRecallSeconds(60 * 60 * 24 * 7), cinergySCookies(
        "")
{
  for (int i = 0; i < 5; ++i)
  {
    updateThreads.emplace_back(new UpdateThread(this));
  }
  this->favoritesOnly = favoritesOnly;
}

TeleBoy::~TeleBoy()
{
  for (auto const &updateThread : updateThreads)
  {
    updateThread->StopThread(200);
    delete updateThread;
  }
}

bool TeleBoy::Login(string u, string p)
{
  Curl curl;
  HttpGet(curl, "https://www.teleboy.ch/login");
  curl.AddHeader("Referer", "https://www.teleboy.ch/login");
  curl.AddHeader("redirect-limit", "0");
  if (!cinergySCookies.empty())
  {
    curl.AddOption("cookie", "cinergy_s=" + cinergySCookies);
  }
  string result = HttpPost(curl, tbUrl + "/login_check",
      "login=" + Utils::UrlEncode(u) + "&password=" + Utils::UrlEncode(p)
          + "&keep_login=1");
  curl.ResetHeaders();
  curl.AddHeader("redirect-limit", "5");
  curl.AddHeader("Referer", "https://www.teleboy.ch/login");
  if (!cinergySCookies.empty())
  {
    curl.AddOption("cookie", "cinergy_s=" + cinergySCookies);
  }
  result = HttpGet(curl, "https://www.teleboy.ch/");
  curl.ResetHeaders();
  if (result.empty())
  {
    XBMC->Log(LOG_ERROR, "Failed to login.");
    return false;
  }

  unsigned int pos = result.find("setId(");
  if (pos == std::string::npos)
  {
    XBMC->Log(LOG_ERROR, "No user settings found.");
    return false;
  }
  pos += 6;
  int endPos = result.find(")", pos);
  userId = result.substr(pos, endPos - pos);
  XBMC->Log(LOG_NOTICE, "Got userId: %s.", userId.c_str());
  return true;
}

void TeleBoy::GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  pCapabilities->bSupportsRecordings = true;
  pCapabilities->bSupportsTimers = true;
}

bool TeleBoy::LoadChannels()
{
  Document json;
  if (!ApiGet("/epg/stations?expand=logos&language=de", json))
  {
    XBMC->Log(LOG_ERROR, "Error loading channels.");
    return false;
  }
  Value& channels = json["data"]["items"];
  for (Value::ConstValueIterator itr1 = channels.Begin();
      itr1 != channels.End(); ++itr1)
  {
    const Value &c = (*itr1);
    if (!c["has_stream"].GetBool())
    {
      continue;
    }
    TeleBoyChannel channel;
    channel.id = c["id"].GetInt();
    channel.name = c["name"].GetString();
    channel.logoPath = "https://media.cinergy.ch/t_station/"
        + to_string(channel.id) + "/icon320_dark.png";
    channelsById[channel.id] = channel;
  }

  if (!ApiGet("/users/" + userId + "/stations", json))
  {
    XBMC->Log(LOG_ERROR, "Error loading sorted channels.");
    return false;
  }
  channels = json["data"]["items"];
  for (Value::ConstValueIterator itr1 = channels.Begin();
      itr1 != channels.End(); ++itr1)
  {
    int cid = (*itr1).GetInt();
    if (channelsById.find(cid) != channelsById.end())
    {
      sortedChannels.push_back(cid);
    }
  }
  return true;
}

int TeleBoy::GetChannelsAmount(void)
{
  if (favoritesOnly)
  {
    return sortedChannels.size();
  }
  return channelsById.size();
}

PVR_ERROR TeleBoy::GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  int channelNum = 0;
  for (int const &cid : sortedChannels)
  {
    channelNum++;
    TransferChannel(handle, channelsById[cid], channelNum);
  }
  if (!favoritesOnly)
  {
    for (auto const &item : channelsById)
    {
      if (std::find(sortedChannels.begin(), sortedChannels.end(), item.first)
          != sortedChannels.end())
      {
        continue;
      }
      channelNum++;
      TransferChannel(handle, item.second, channelNum);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

void TeleBoy::TransferChannel(ADDON_HANDLE handle, TeleBoyChannel channel,
    int channelNum)
{
  PVR_CHANNEL kodiChannel;
  memset(&kodiChannel, 0, sizeof(PVR_CHANNEL));

  kodiChannel.iUniqueId = channel.id;
  kodiChannel.bIsRadio = false;
  kodiChannel.iChannelNumber = channelNum;
  PVR_STRCPY(kodiChannel.strChannelName, channel.name.c_str());
  PVR_STRCPY(kodiChannel.strIconPath, channel.logoPath.c_str());
  PVR->TransferChannelEntry(handle, &kodiChannel);
}

string TeleBoy::GetChannelStreamUrl(int uniqueId)
{
  Document json;
  if (!ApiGet(
      "/users/" + userId + "/stream/live/" + to_string(uniqueId)
          + "?alternative=false", json))
  {
    XBMC->Log(LOG_ERROR, "Error getting live stream url for channel %i.",
        uniqueId);
    return "";
  }
  string url = json["data"]["stream"]["url"].GetString();
  XBMC->Log(LOG_ERROR, "Play URL: %s.", url.c_str());
  return url;
}

void TeleBoy::GetEPGForChannel(const PVR_CHANNEL &channel, time_t iStart,
    time_t iEnd)
{
  UpdateThread::LoadEpg(channel.iUniqueId, iStart, iEnd);
}

void TeleBoy::GetEPGForChannelAsync(int uniqueChannelId, time_t iStart,
    time_t iEnd)
{
  int totals = -1;
  int sum = 0;
  while (totals == -1 || sum < totals)
  {
    Document json;
    if (!ApiGet(
        "/users/" + userId + "/broadcasts?begin=" + formatDateTime(iStart)
            + "&end=" + formatDateTime(iEnd) + "&expand=logos&limit=500&skip="
            + to_string(sum) + "&sort=station&station="
            + to_string(uniqueChannelId), json))
    {
      XBMC->Log(LOG_ERROR, "Error getting epg for channel %i.",
          uniqueChannelId);
      return;
    }
    totals = json["data"]["total"].GetInt();
    const Value& items = json["data"]["items"];
    for (Value::ConstValueIterator itr1 = items.Begin(); itr1 != items.End();
        ++itr1)
    {
      const Value& item = (*itr1);
      sum++;
      EPG_TAG tag;
      memset(&tag, 0, sizeof(EPG_TAG));
      tag.iUniqueBroadcastId = item["id"].GetInt();
      tag.strTitle = strdup(item["title"].GetString());
      tag.iUniqueChannelId = uniqueChannelId;
      tag.startTime = StringToTime(item["begin"].GetString());
      tag.endTime = StringToTime(item["end"].GetString());
      tag.strPlotOutline = nullptr; /* not supported */
      tag.strPlot = nullptr; /* not supported */
      tag.strOriginalTitle =
          item.HasMember("original_title") ?
              strdup(item["original_title"].GetString()) : nullptr;
      tag.strCast = nullptr; /* not supported */
      tag.strDirector = nullptr; /*SA not supported */
      tag.strWriter = nullptr; /* not supported */
      tag.iYear = item["year"].GetInt();
      tag.strIMDBNumber = nullptr; /* not supported */
      tag.strIconPath = nullptr; /* not supported */
      tag.iParentalRating = 0; /* not supported */
      tag.iStarRating = 0; /* not supported */
      tag.bNotify = false; /* not supported */
      tag.iSeriesNumber = 0; /* not supported */
      tag.iEpisodeNumber = 0; /* not supported */
      tag.iEpisodePartNumber = 0; /* not supported */
      tag.strEpisodeName =
          item.HasMember("subtitle") ?
              strdup(item["subtitle"].GetString()) : "";
      ; /* not supported */
      tag.iGenreType = EPG_GENRE_USE_STRING;
      tag.strGenreDescription = strdup(item["type"].GetString());
      tag.iFlags = EPG_TAG_FLAG_UNDEFINED;

      PVR->EpgEventStateChange(&tag, EPG_EVENT_CREATED);
    }
    XBMC->Log(LOG_DEBUG, "Loaded %i of %i epg entries for channel %i.", sum,
        totals, uniqueChannelId);
  }
  return;
}

string TeleBoy::formatDateTime(time_t dateTime)
{
  char buff[20];
  strftime(buff, 20, "%Y-%m-%d+%H:%M:%S", gmtime(&dateTime));
  return buff;
}

bool TeleBoy::Record(int programId)
{
  string postData = "{\"broadcast\": " + to_string(programId)
      + ", \"alternative\": false}";
  Document json;
  if (!ApiPost("/users/" + userId + "/recordings", postData, json))
  {
    XBMC->Log(LOG_ERROR, "Error recording program %i.", programId);
    return false;
  }
  return true;
}

bool TeleBoy::DeleteRecording(string recordingId)
{
  Document doc;
  if (!ApiDelete("/users/" + userId + "/recordings/" + recordingId, doc))
  {
    XBMC->Log(LOG_ERROR, "Error deleting recording %s.", recordingId.c_str());
    return false;
  }
  return true;
}

void TeleBoy::GetRecordings(ADDON_HANDLE handle, string type)
{
  int totals = -1;
  int sum = 0;
  while (totals == -1 || sum < totals)
  {
    Document json;
    if (!ApiGet(
        "/users/" + userId + "/recordings/" + type
            + "?desc=1&expand=flags,logos&limit=100&skip=0&sort=date", json))
    {
      XBMC->Log(LOG_ERROR, "Error getting recordings of type %s.",
          type.c_str());
      return;
    }
    totals = json["data"]["total"].GetInt();
    const Value& items = json["data"]["items"];
    for (Value::ConstValueIterator itr1 = items.Begin(); itr1 != items.End();
        ++itr1)
    {
      const Value& item = (*itr1);
      sum++;

      if (type.find("planned") == 0)
      {
        PVR_TIMER tag;
        memset(&tag, 0, sizeof(PVR_TIMER));

        tag.iClientIndex = item["id"].GetInt();
        PVR_STRCPY(tag.strTitle, item["title"].GetString());
        if (item.HasMember("subtitle"))
        {
          PVR_STRCPY(tag.strSummary, item["subtitle"].GetString());
        }
        tag.startTime = StringToTime(item["begin"].GetString());
        tag.endTime = StringToTime(item["end"].GetString());
        tag.state = PVR_TIMER_STATE_SCHEDULED;
        tag.iTimerType = 1;
        tag.iEpgUid = item["id"].GetInt();
        tag.iClientChannelUid = item["station_id"].GetInt();
        PVR->TransferTimerEntry(handle, &tag);
        UpdateThread::SetNextRecordingUpdate(tag.endTime + 60 * 21);

      }
      else
      {
        PVR_RECORDING tag;
        memset(&tag, 0, sizeof(PVR_RECORDING));
        tag.bIsDeleted = false;
        PVR_STRCPY(tag.strRecordingId, to_string(item["id"].GetInt()).c_str());
        PVR_STRCPY(tag.strTitle, item["title"].GetString());
        if (item.HasMember("subtitle"))
        {
          PVR_STRCPY(tag.strEpisodeName, item["subtitle"].GetString());
        }
        if (item.HasMember("description"))
        {
          PVR_STRCPY(tag.strPlot, item["description"].GetString());
        }
        if (item.HasMember("short_description"))
        {
          PVR_STRCPY(tag.strPlotOutline, item["short_description"].GetString());
        }
        tag.iChannelUid = item["station_id"].GetInt();
        PVR_STRCPY(tag.strIconPath,
            channelsById[tag.iChannelUid].logoPath.c_str());
        PVR_STRCPY(tag.strChannelName,
            channelsById[tag.iChannelUid].name.c_str());
        tag.recordingTime = StringToTime(item["begin"].GetString());
        time_t endTime = StringToTime(item["end"].GetString());
        tag.iDuration = endTime - tag.recordingTime;
        tag.iEpgEventId = item["id"].GetInt();

        PVR->TransferRecordingEntry(handle, &tag);
      }
    }
  }
}

string TeleBoy::GetRecordingStreamUrl(string recordingId)
{
  Document json;
  if (!ApiGet("/users/" + userId + "/stream/recording/" + recordingId, json))
  {
    XBMC->Log(LOG_ERROR, "Could not get URL for recording: %s.",
        recordingId.c_str());
    return "";
  }
  string url = json["data"]["stream"]["url"].GetString();
  return url;
}

bool TeleBoy::IsPlayable(const EPG_TAG *tag)
{
  time_t current_time;
  time(&current_time);
  return ((current_time - tag->endTime) < maxRecallSeconds)
      && (tag->startTime < current_time);
}

bool TeleBoy::IsRecordable(const EPG_TAG *tag)
{
  time_t current_time;
  time(&current_time);
  return ((current_time - tag->endTime) < maxRecallSeconds);
}

string TeleBoy::GetEpgTagUrl(const EPG_TAG *tag)
{
  Document json;
  if (!ApiGet(
      "/users/" + userId + "/stream/replay/"
          + to_string(tag->iUniqueBroadcastId), json))
  {
    XBMC->Log(LOG_ERROR, "Could not get URL for epg tag.");
    return "";
  }
  string url = json["data"]["stream"]["url"].GetString();
  return url;
}

time_t TeleBoy::StringToTime(string timeString)
{
  struct tm *tm;
  time_t current_time;
  time(&current_time);
  tm = localtime(&current_time);

  int year, month, day, h, m, s;
  sscanf(timeString.c_str(), "%d-%d-%dT%d:%d:%dZ", &year, &month, &day, &h, &m,
      &s);
  tm->tm_year = year - 1900;
  tm->tm_mon = month - 1;
  tm->tm_mday = day;
  tm->tm_hour = h;
  tm->tm_min = m;
  tm->tm_sec = s;

  return mktime(tm);
}
