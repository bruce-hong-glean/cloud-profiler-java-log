/*
 * Copyright 2018 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef THIRD_PARTY_JAVAPROFILER_PROFILE_PROTO_BUILDER_H__
#define THIRD_PARTY_JAVAPROFILER_PROFILE_PROTO_BUILDER_H__

#include <jvmti.h>
#include <link.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "perftools/profiles/proto/builder.h"
#include "third_party/javaprofiler/method_info.h"
#include "third_party/javaprofiler/stacktrace_decls.h"

namespace google {
namespace javaprofiler {

// Value and unit for a numerical label.
struct NumLabelValue {
  // Actual value of the label.
  const int64_t value;

  // Unit for the numerical label.
  const std::string unit;

  // Constructor with a value and unit name.
  NumLabelValue(int64_t v = 0, const std::string &u = "") : value(v), unit(u) {}

  // Equality operator with another NumLabelValue.
  bool operator==(const NumLabelValue &other) const {
    return value == other.value && unit == other.unit;
  }

  // Hash method for this numerical label.
  size_t Hash(size_t current_hash_value) const;
};

// Label associated with a sample.
struct SampleLabel {
  // Default constructor creating an empty string label.
  SampleLabel() : key(""), is_string_label(true) {}

  // Constructor for a string label.
  SampleLabel(const std::string &k, const std::string &str)
      : key(k), str_label(str), is_string_label(true) {}

  // Constructor for a numerical label.
  SampleLabel(const std::string &k, int64_t num, const std::string &unit = "")
      : key(k), num_label(num, unit), is_string_label(false) {}

  // The label key.
  const std::string key;

  // String value if this SampleLabel is_string_label.
  const std::string str_label;
  // Numeric value if this SampleLabel not is_string_label.
  const NumLabelValue num_label;

  // Content of the value "union".
  const bool is_string_label;

  // Equality operator for the label.
  bool operator==(const SampleLabel &other) const;

  // Hash method for this numerical label.
  size_t Hash(size_t current_hash_value) const;
};

// Trace and labels stored together for storage in the hash map while
// constructing the profile proto.
struct TraceAndLabels {
  // Constructor for a trace without labels.
  TraceAndLabels(const JVMPI_CallTrace *t) : trace(t) {}

  // Constructor for a trace with labels.
  TraceAndLabels(const JVMPI_CallTrace *t, const std::vector<SampleLabel> &l)
      : trace(t), labels(l) {}

  // Adds a string label to the associated trace.
  void AddLabel(const std::string &key, const std::string &str_value) {
    labels.push_back({key, str_value});
  }

  // Adds a numeric label to the associated trace.
  void AddLabel(const std::string &key, int64_t num_value,
                const std::string &unit = "") {
    labels.push_back({key, num_value, unit});
  }

  // Trace associated with given labels.
  const JVMPI_CallTrace *trace;
  // Labels associated with the trace.
  std::vector<SampleLabel> labels;
};

// A profile stack trace containing a stack trace, a metric value, and any
// potential labels associated with this trace.
struct ProfileStackTrace {
  // Constructor for a stack trace without any label.
  ProfileStackTrace(const JVMPI_CallTrace *trace = nullptr, int64_t m_value = 0)
      : metric_value(m_value), trace_and_labels(trace) {}

  // Constructor for a stack trace with labels.
  ProfileStackTrace(const JVMPI_CallTrace *trace, int64_t m_value,
                    const std::vector<SampleLabel> &labels)
      : metric_value(m_value), trace_and_labels(trace, labels) {}

  // Metric associated with the trace and labels.
  int64_t metric_value;

  // Trace and labels associated with the metric collected.
  TraceAndLabels trace_and_labels;
};

// Store proto sample objects for specific stack traces and label values.
class TraceSamples {
 public:
  // Returns an existing sample with the same trace and labels if exists,
  // nullptr otherwise.
  perftools::profiles::Sample *SampleFor(const TraceAndLabels &trace) const;

  // Adds a given trace and label to storage and associates it with a given
  // sample.
  void Add(const TraceAndLabels &trace, perftools::profiles::Sample *sample);

 private:
  struct TraceHash {
    size_t operator()(const TraceAndLabels &trace_values) const;
  };

  struct TraceEquals {
    bool operator()(const TraceAndLabels &trace_values1,
                    const TraceAndLabels &trace_values2) const;
  };

  std::unordered_map<TraceAndLabels, perftools::profiles::Sample *, TraceHash,
                     TraceEquals>
      traces_;
};

enum class NativeSymbolization {
  SYMBOLS,
  NO_SYMBOLS
};

// Store locations previously seen so that the profile is only
// modified for new locations.
class LocationBuilder {
 public:
  explicit LocationBuilder(perftools::profiles::Builder *builder)
      : builder_(builder) {}

  // Return an existing or new location matching the given parameters,
  // modifying the profile as needed to add new function and location
  // information.
  perftools::profiles::Location *LocationFor(const std::string &class_name,
                                             const std::string &function_name,
                                             const std::string &file_name,
                                             int start_line,
                                             int line_number,
                                             int64_t address);

 private:
  struct LocationInfo {
    std::string class_name;
    std::string function_name;
    std::string file_name;
    int line_number;
    int64_t address;
  };

  struct LocationInfoHash {
    size_t operator()(const LocationInfo &info) const;
  };

  struct LocationInfoEquals {
    bool operator()(const LocationInfo &info1, const LocationInfo &info2) const;
  };

  perftools::profiles::Builder *builder_;

  std::unordered_map<LocationInfo, perftools::profiles::Location *,
      LocationInfoHash, LocationInfoEquals> locations_;
};

// Remember traces and use the information to create locations with native
// information if supported.
class ProfileFrameCache {
 public:
  virtual void ProcessTraces(const ProfileStackTrace *traces,
                             int num_traces) = 0;

  virtual perftools::profiles::Location *GetLocation(
      const JVMPI_CallFrame &jvm_frame, LocationBuilder *location_builder) = 0;

  virtual std::string GetFunctionName(const JVMPI_CallFrame &jvm_frame) = 0;

  virtual ~ProfileFrameCache() {}
};

// Caching jmethodID resolution:
//   - This allows a one-time calculation of a given jmethodID during proto
//     creation.
//   - The cache reduces the number of JVMTI calls to symbolize the stacks.
//   - jmethodIDs are never invalid per se or re-used: if ever the jmethodID's
//     class is unloaded, the JVMTI calls will return an error code that
//     caught by the various JVMTI calls done.
// Though it would theoretically be possible to cache the jmethodID for the
// lifetime of the program, this just implementation keeps the cache live
// during this proto creation. The reason is that jmethodIDs might become
// stale/unloaded and there would need to be extra work to determine
// cache-size management.
class MethodInfoCache {
 public:
  MethodInfoCache(JNIEnv *jni_env, jvmtiEnv *jvmti_env);

  MethodInfo *Method(jmethodID id);

 private:
  JNIEnv *jni_env_;
  jvmtiEnv *jvmti_env_;

  std::unordered_map<jmethodID, std::unique_ptr<MethodInfo>> methods_;
};

// Create profile protobufs from traces obtained from JVM profiling.
class ProfileProtoBuilder {
 public:
  virtual ~ProfileProtoBuilder() {}

  // Adds traces to the proto. The traces array must not be deleted
  // before calling CreateProto.
  void AddTraces(const ProfileStackTrace *traces, int num_traces);

  // Adds traces to the proto, where each trace has a defined count
  // of occurrences. The traces array must not be deleted
  // before calling CreateProto.
  void AddTraces(const ProfileStackTrace *traces, const int32_t *counts,
                 int num_traces);

  // Adds a "fake" trace with a single frame. Used to represent JVM
  // tasks such as JIT compilation and GC.
  void AddArtificialTrace(const std::string &name, int count,
                          int sampling_rate);

  // Builds the proto. Calling any other method on the class after calling
  // this has undefined behavior.
  virtual std::unique_ptr<perftools::profiles::Profile> CreateProto() = 0;

  // Creates a heap profile.
  // jvmti_env can be null but then all calls to AddTraces will return
  // unknown.
  // Accepts a null cache since the heap profiles can be using JVMTI's
  // GetStackTrace and remain in pure Java land frames. Other ForX methods
  // will fail an assertion when attempting a nullptr cache.
  static std::unique_ptr<ProfileProtoBuilder> ForHeap(
      JNIEnv *jni_env, jvmtiEnv *jvmti_env, int64_t sampling_rate,
      ProfileFrameCache *cache = nullptr);

  static std::unique_ptr<ProfileProtoBuilder> ForCpu(JNIEnv *jni_env,
                                                     jvmtiEnv *jvmti_env,
                                                     int64_t duration_ns,
                                                     int64_t sampling_rate,
                                                     ProfileFrameCache *cache);

  static std::unique_ptr<ProfileProtoBuilder> ForContention(
      JNIEnv *jni_env, jvmtiEnv *jvmti_env, int64_t duration_ns,
      int64_t sampling_rate, ProfileFrameCache *cache);

 protected:
  struct SampleType {
    SampleType(const std::string &type_in, const std::string &unit_in)
        : type(type_in), unit(unit_in) {}

    std::string type;
    std::string unit;
  };

  // Create the profile proto builder, if native_cache is nullptr, then no
  // information about native frames can be provided. The proto buffer will then
  // contain "Unknown native method" frames.
  ProfileProtoBuilder(JNIEnv *env, jvmtiEnv *jvmti_env,
                      ProfileFrameCache *native_cache, int64_t sampling_rate,
                      const SampleType &count_type,
                      const SampleType &metric_type,
                      bool skip_top_native_frames,
                      std::vector<std::string> skip_frames);

  // Build the proto, unsampling the sample metrics. Calling any other method
  // on the class after calling this has undefined behavior.
  std::unique_ptr<perftools::profiles::Profile> CreateUnsampledProto();

  // Build the proto, without normalizing the sampled metrics. Calling any
  // other method on the class after calling this has undefined behavior.
  std::unique_ptr<perftools::profiles::Profile> CreateSampledProto();

  perftools::profiles::Builder builder_;
  int64_t sampling_rate_ = 0;

 private:
  bool SkipFrame(const std::string &function_name) const;
  int SkipTopNativeFrames(const JVMPI_CallTrace &trace);
  void AddSampleType(const SampleType &sample_type);
  void SetPeriodType(const SampleType &metric_type);
  void InitSampleValues(perftools::profiles::Sample *sample, int64_t count,
                        int64_t metric);
  void UpdateSampleValues(perftools::profiles::Sample *sample, int64_t count,
                          int64_t size);
  void AddTrace(const ProfileStackTrace &trace, int32_t count);
  void AddJavaInfo(const JVMPI_CallFrame &jvm_frame,
                   perftools::profiles::Profile *profile,
                   perftools::profiles::Sample *sample);
  void AddNativeInfo(const JVMPI_CallFrame &jvm_frame,
                     perftools::profiles::Profile *profile,
                     perftools::profiles::Sample *sample);
  void UnsampleMetrics();

  int64_t Location(MethodInfo *method, const JVMPI_CallFrame &frame);

  void AddLabels(const TraceAndLabels &trace_and_labels,
                 perftools::profiles::Sample *sample);

  JNIEnv *jni_env_;
  jvmtiEnv *jvmti_env_;

  MethodInfoCache method_info_cache_;
  ProfileFrameCache *native_cache_;
  TraceSamples trace_samples_;
  LocationBuilder location_builder_;

  bool skip_top_native_frames_;
};

// Computes the ratio to use to scale heap data to unsample it.
// Accounts for the probability of it appearing in the
// collected data based on exponential samples. heap profiles rely
// on a poisson process to determine which samples to collect, based
// on the desired average collection rate R. The probability of a
// sample of size S to appear in that profile is 1-exp(-S/R).
double CalculateSamplingRatio(int64_t rate, int64_t count,
                              int64_t metric_value);

class CpuProfileProtoBuilder : public ProfileProtoBuilder {
 public:
  CpuProfileProtoBuilder(JNIEnv *jni_env, jvmtiEnv *jvmti_env,
                         int64_t duration_ns, int64_t sampling_rate,
                         ProfileFrameCache *cache,
                         bool skip_top_native_frames,
                         std::vector<std::string> skip_frames)
      : ProfileProtoBuilder(
            jni_env, jvmti_env, cache, sampling_rate,
            ProfileProtoBuilder::SampleType("samples", "count"),
            ProfileProtoBuilder::SampleType("cpu", "nanoseconds"),
            skip_top_native_frames,
            skip_frames) {
    builder_.mutable_profile()->set_duration_nanos(duration_ns);
    builder_.mutable_profile()->set_period(sampling_rate);
  }

  std::unique_ptr<perftools::profiles::Profile> CreateProto() override {
    return CreateSampledProto();
  }
};

class HeapProfileProtoBuilder : public ProfileProtoBuilder {
 public:
  HeapProfileProtoBuilder(JNIEnv *jni_env, jvmtiEnv *jvmti_env,
                          int64_t sampling_rate, ProfileFrameCache *cache,
                          bool skip_top_native_frames,
                          std::vector<std::string> skip_frames)
      : ProfileProtoBuilder(
            jni_env, jvmti_env, cache, sampling_rate,
            ProfileProtoBuilder::SampleType("inuse_objects", "count"),
            ProfileProtoBuilder::SampleType("inuse_space", "bytes"),
            skip_top_native_frames,
            skip_frames)
        {
  }

  std::unique_ptr<perftools::profiles::Profile> CreateProto() override {
    return CreateUnsampledProto();
  }
};

class ContentionProfileProtoBuilder : public ProfileProtoBuilder {
 public:
  ContentionProfileProtoBuilder(JNIEnv *jni_env, jvmtiEnv *jvmti_env,
                                int64_t duration_nanos, int64_t sampling_rate,
                                ProfileFrameCache *cache,
                                bool skip_top_native_frames,
                                std::vector<std::string> skip_frames)
      : ProfileProtoBuilder(
            jni_env, jvmti_env, cache, sampling_rate,
            ProfileProtoBuilder::SampleType("contentions", "count"),
            ProfileProtoBuilder::SampleType("delay", "microseconds"),
            skip_top_native_frames,
            skip_frames) {
    builder_.mutable_profile()->set_duration_nanos(duration_nanos);
    builder_.mutable_profile()->set_period(sampling_rate);
    builder_.mutable_profile()->set_default_sample_type(
        builder_.StringId("delay"));
  }

  std::unique_ptr<perftools::profiles::Profile> CreateProto() {
    MultiplyBySamplingRate();
    builder_.Finalize();
    return std::unique_ptr<perftools::profiles::Profile>(builder_.Consume());
  }

 private:
  // Multiply the (count, metric) values by the sampling_rate.
  void MultiplyBySamplingRate();
};

}  // namespace javaprofiler
}  // namespace google

#endif  // THIRD_PARTY_JAVAPROFILER_PROFILE_PROTO_BUILDER_H__
