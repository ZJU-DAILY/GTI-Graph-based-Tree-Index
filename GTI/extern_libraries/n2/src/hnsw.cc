// Copyright 2017 Kakao Corp. <http://www.kakaocorp.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "n2/hnsw.h"

namespace n2
{

    using std::move;
    using std::pair;
    using std::runtime_error;
    using std::string;
    using std::to_string;
    using std::vector;

    Hnsw::Hnsw() : Hnsw(0) {}

    Hnsw::Hnsw(int dim, string metric) : data_dim_(dim)
    {
        if (metric == "L2" || metric == "euclidean")
        {
            metric_ = DistanceKind::L2;
        }
        else if (metric == "angular")
        {
            metric_ = DistanceKind::ANGULAR;
        }
        else if (metric == "dot")
        {
            metric_ = DistanceKind::DOT;
        }
        else
        {
            throw runtime_error("[Error] Invalid configuration value for DistanceMethod: " + metric);
        }
    }

    Hnsw::Hnsw(const Hnsw &other)
    {
        *this = other;
    }

    Hnsw::Hnsw(Hnsw &&other) noexcept
    {
        *this = move(other);
    }

    Hnsw &Hnsw::operator=(const Hnsw &other)
    {
        if (this != &other)
        {
            model_ = other.model_;
            data_dim_ = other.data_dim_;
            metric_ = other.metric_;
            InitSearcherAndSearcherPool_();
            ensure_k_ = other.ensure_k_;
        }
        return *this;
    }

    Hnsw &Hnsw::operator=(Hnsw &&other) noexcept
    {
        if (this != &other)
        {
            model_ = move(other.model_);
            searcher_ = move(other.searcher_);
            searcher_pool_ = move(other.searcher_pool_);
            data_dim_ = other.data_dim_;
            metric_ = other.metric_;
            ensure_k_ = other.ensure_k_;
        }
        return *this;
    }

    Hnsw::~Hnsw()
    {
    }

    void Hnsw::AddData(const vector<float> &data)
    {
        // if (model_ != nullptr)
        // {
        //     throw runtime_error("[Error] This index already has a trained model. Adding an item is not allowed.");
        // }
        if (builder_ == nullptr)
        {
            builder_ = HnswBuild::GenerateBuilder(data_dim_, metric_);
        }
        if (builder_)
        {
            builder_->AddData(data);
        }
    }

    void Hnsw::AddDataM(const vector<float> &data)
    {
        if (builder_)
        {
            builder_->AddDataM(data);
        }
    }

    void Hnsw::SetConfigs(const vector<pair<string, string>> &configs)
    {
        if (builder_ == nullptr and model_ == nullptr)
        {
            builder_ = HnswBuild::GenerateBuilder(data_dim_, metric_);
        }
        if (builder_)
        {
            builder_->SetConfigs(configs);
        }
        for (const auto &c : configs)
        {
            if (c.first == "EnsureK")
            {
                if (c.second == "true")
                {
                    ensure_k_ = true;
                }
                else
                {
                    ensure_k_ = false;
                }
            }
        }
    }

    void Hnsw::Build(int m, int max_m0, int ef_construction, int n_threads, float mult,
                     NeighborSelectingPolicy neighbor_selecting, GraphPostProcessing graph_merging, bool ensure_k)
    {
        if (model_ != nullptr)
        {
            throw runtime_error("[Error] This index already has a trained model. Building an index is not allowed.");
        }
        if (builder_ == nullptr)
        {
            builder_ = HnswBuild::GenerateBuilder(data_dim_, metric_);
        }
        model_ = builder_->Build(m, max_m0, ef_construction, n_threads, mult, neighbor_selecting, graph_merging);
        InitSearcherAndSearcherPool_();
        // builder_.reset();
        ensure_k_ = ensure_k;
    }

    // Save nodes
    void Hnsw::saveNodes(const std::string &file_name)
    {
        builder_->saveNodes(file_name);
        builder_.reset();
    }

    // Load nodes
    void Hnsw::loadNodes(const std::string &file_name)
    {
        if (builder_ == nullptr)
        {
            builder_ = HnswBuild::GenerateBuilder(data_dim_, metric_);
        }

        builder_->loadNodes(file_name);
    }

    // Build graph from insertion
    void Hnsw::buildFromInsert()
    {
        if (model_ != nullptr)
        {
            throw runtime_error("[Error] This index already has a trained model. Building an index is not allowed.");
        }
        if (builder_ == nullptr)
        {
            throw runtime_error("[Error] No data to fit. Load data first.");
        }
        model_ = builder_->buildFromInsert();
        InitSearcherAndSearcherPool_();
    }

    // Get in degree radius of node id
    float Hnsw::getRadius(unsigned id)
    {
        return builder_->getRadius(id);
    }

    // Delete neighbor
    void Hnsw::deleteNeighbor(int source, int neighbor, std::vector<unsigned> &reinsert_ids)
    {
        builder_->deleteNeighbor(source, neighbor, reinsert_ids);
    }

    // Delete graph data
    void Hnsw::deleteData(int id)
    {
        builder_->deleteData(id);
    }

    // Build graph from deletion
    void Hnsw::buildFromDeletion()
    {
        if (model_ != nullptr)
        {
            throw runtime_error("[Error] This index already has a trained model. Building an index is not allowed.");
        }
        if (builder_ == nullptr)
        {
            throw runtime_error("[Error] No data to fit. Load data first.");
        }
        model_ = builder_->buildFromDeletion();
        InitSearcherAndSearcherPool_();
    }

    // Reinsert data into graph
    void Hnsw::reinsertData(std::vector<unsigned> reinsert_gids)
    {
        builder_->reinsertData(reinsert_gids);
    }

    // Check whether the data is enter point
    bool Hnsw::checkEnter(int id)
    {
        builder_->checkEnter(id);
    }

    // Update enter point
    void Hnsw::updateEnter(int id)
    {
        builder_->updateEnter(id);
    }

    void Hnsw::Fit()
    {
        if (builder_ == nullptr)
        {
            throw runtime_error("[Error] No data to fit. Load data first.");
        }
        model_ = builder_->Build();
        InitSearcherAndSearcherPool_();
        builder_.reset();
    }

    bool Hnsw::SaveModel(const string &fname) const
    {
        return model_->SaveModelToFile(fname);
    }

    bool Hnsw::LoadModel(const string &fname, const bool use_mmap)
    {
        model_ = HnswModel::LoadModelFromFile(fname, use_mmap);
        size_t model_data_dim = model_->GetDataDim();
        if (data_dim_ > 0 && data_dim_ != model_data_dim)
        {
            throw runtime_error("[Error] index dimension(" + to_string(data_dim_) + ") != model dimension(" + to_string(model_data_dim) + ")");
        }
        data_dim_ = model_data_dim;
        metric_ = model_->GetMetric();
        InitSearcherAndSearcherPool_();
        return true;
    }

    void Hnsw::UnloadModel()
    {
        if (model_ != nullptr)
        {
            model_.reset();
        }
        if (searcher_ != nullptr)
        {
            searcher_.reset();
        }
        searcher_pool_.clear();
    }

    void Hnsw::PrintConfigs() const
    {
        builder_->PrintConfigs();
    }

    void Hnsw::PrintDegreeDist() const
    {
        builder_->PrintDegreeDist();
    }

    void Hnsw::InitSearcherAndSearcherPool_()
    {
        searcher_ = HnswSearch::GenerateSearcher(model_, data_dim_, metric_);
        searcher_pool_.clear();
        // the searcher_ is used for both single-thread search and multi-thread(batch) search
        searcher_pool_.push_back(searcher_);
    }

} // namespace n2
