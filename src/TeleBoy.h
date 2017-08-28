#include "client.h"
#include "UpdateThread.h"
#include "Curl.h"
#include <map>
#include "rapidjson/document.h"

using namespace std;
using namespace rapidjson;

/*!
 * @brief PVR macros for string exchange
 */
#define PVR_STRCPY(dest, source) do { strncpy(dest, source, sizeof(dest)-1); dest[sizeof(dest)-1] = '\0'; } while(0)
#define PVR_STRCLR(dest) memset(dest, 0, sizeof(dest))

struct TeleBoyChannel
{
  int id;
  std::string name;
  std::string logoPath;
};

struct PVRIptvEpgGenre
{
  int iGenreType;
  int iGenreSubType;
  std::string strGenre;
};

class TeleBoy
{
public:
  TeleBoy(bool favoritesOnly);
  virtual ~TeleBoy();
  virtual bool Login(string u, string p);
  virtual bool LoadChannels();
  virtual void GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities);
  virtual int GetChannelsAmount(void);
  virtual PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio);
  virtual void TransferChannel(ADDON_HANDLE handle, TeleBoyChannel channel,
      int channelNum);
  virtual std::string GetChannelStreamUrl(int uniqueId);
  virtual PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle,
      const PVR_CHANNEL &channel, time_t iStart, time_t iEnd);
  virtual void GetRecordings(ADDON_HANDLE handle, string type);
  virtual string GetRecordingStreamUrl(string recordingId);
  virtual bool Record(int programId);
  virtual bool DeleteRecording(string recordingId);
  virtual bool IsPlayable(const EPG_TAG *tag);
  virtual bool IsRecordable(const EPG_TAG *tag);
  virtual string GetEpgTagUrl(const EPG_TAG *tag);

private:
  string username;
  string password;
  bool favoritesOnly;
  string userId;
  map<int, TeleBoyChannel> channelsById;
  vector<int> sortedChannels;
  int64_t maxRecallSeconds;
  Curl *curl;
  UpdateThread *updateThread;

  virtual string formatDateTime(time_t dateTime);
  virtual string HttpGet(string url);
  virtual void ApiSetHeader();
  virtual bool ApiGetResult(string content, Document &doc);
  virtual bool ApiGet(string url, Document &doc);
  virtual bool ApiPost(string url, string postData, Document &doc);
  virtual bool ApiDelete(string url, Document &doc);
  virtual string HttpPost(string url, string postData);
  time_t StringToTime(string timeString);
};
