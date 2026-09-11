#ifndef AUDIOXL_STREAM_HPP
#define AUDIOXL_STREAM_HPP

// Incremental decoding for long rows. (￣ω￣;)

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

namespace AudioXLNS {

class SoundData;

class StreamDecoder {
 public:
  ~StreamDecoder();

  bool Open(const std::string& aExt, std::shared_ptr<SoundData> aData, std::string& aWhy);

  uint32_t Rate() const { return m_rate; }
  uint32_t Channels() const { return m_channels; }

  uint64_t Read(int16_t* aOut, uint64_t aFrames);
  bool Seek(uint64_t aFrame);

 private:
  void Close();

  enum class Kind { None, Mp3, Flac, Vorbis };
  Kind m_kind = Kind::None;
  void* m_handle = nullptr;               
  std::shared_ptr<SoundData> m_data;      
  uint32_t m_rate = 0;
  uint32_t m_channels = 0;
};

class StreamRow {
 public:
  StreamRow(std::string aPath, std::string aExt, uint32_t aRate, uint32_t aChannels,
            uint64_t aTotalFrames);
  ~StreamRow();

  uint32_t Rate() const { return m_rate; }
  uint32_t Channels() const { return m_channels; }
  uint64_t TotalFrames() const { return m_totalFrames; }

  void RequestSeek(uint64_t aFrame);
  
  void RequestIdle();
  
  uint64_t Take(int16_t* aOut, uint64_t aFrames);
  
  uint64_t Position() const { return m_readFrame.load(std::memory_order_acquire); }
  
  bool Exhausted() const;

  bool Service();

 private:
  bool EnsureOpen();
  void Release();

  static constexpr uint64_t kRingFrames = 96000;   

  std::string m_path;
  std::string m_ext;
  uint32_t m_rate = 0;
  uint32_t m_channels = 0;
  uint64_t m_totalFrames = 0;

  std::unique_ptr<StreamDecoder> m_decoder;
  
  std::vector<int16_t> m_ring;              
  std::atomic<uint64_t> m_write{0};         
  std::atomic<uint64_t> m_read{0};          
  std::atomic<uint64_t> m_seekTo{0};
  std::atomic<uint32_t> m_seekGen{0};       
  std::atomic<uint32_t> m_seekDone{0};      
  std::atomic<uint64_t> m_readFrame{0};     
  std::atomic<bool> m_wanted{false};        
  std::atomic<bool> m_ended{false};         
  uint64_t m_pumpFrame = 0;                 
};

class StreamPump {
 public:
  static StreamPump* Get();

  StreamRow* Add(std::unique_ptr<StreamRow> aRow);
  void Start();
  void Stop();
  size_t Count() const;

 private:
  StreamPump() = default;

  void Run();

  mutable std::mutex m_mutex;
  std::vector<std::unique_ptr<StreamRow>> m_rows;
  std::vector<StreamRow*> m_live;           
  std::thread m_thread;
  std::atomic<bool> m_running{false};
};

}  

#endif  
