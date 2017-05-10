/**
* This file is part of ORB-SLAM2.
*
* Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <https://github.com/raulmur/ORB_SLAM2>
*
* ORB-SLAM2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM2 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.
*/

#include "KeyFrameDatabase.h"

#include "KeyFrame.h"
#include "Thirdparty/DBoW2/DBoW2/BowVector.h"

#include<mutex>

using namespace std;

namespace ORB_SLAM2 {

    KeyFrameDatabase::KeyFrameDatabase(const ORBVocabulary &voc) :
            mpVoc(&voc) {
        mvInvertedFile.resize(voc.size());
    }


    void KeyFrameDatabase::add(KeyFrame *pKF) {
        unique_lock<mutex> lock(mMutex);

        for (DBoW2::BowVector::const_iterator vit = pKF->mBowVec.begin(), vend = pKF->mBowVec.end();
             vit != vend; vit++) {
            LightKeyFrame tLKF(pKF);
            mvInvertedFile[vit->first].push_back(tLKF);
        }
    }

    void KeyFrameDatabase::erase(KeyFrame *pKF) {
        unique_lock<mutex> lock(mMutex);
        LightKeyFrame tLKF(pKF);
        // Erase elements in the Inverse File for the entry
        for (DBoW2::BowVector::const_iterator vit = pKF->mBowVec.begin(), vend = pKF->mBowVec.end();
             vit != vend; vit++) {
            // List of keyframes that share the word
            list<LightKeyFrame> &lLKFs = mvInvertedFile[vit->first];

            for (list<LightKeyFrame>::iterator lit = lLKFs.begin(), lend = lLKFs.end(); lit != lend; lit++) {

                if (tLKF == *lit) {
                    lLKFs.erase(lit);
                    break;
                }
            }
        }
    }

    void KeyFrameDatabase::clear() {
        mvInvertedFile.clear();
        mvInvertedFile.resize(mpVoc->size());
    }

    vector<KeyFrame *> KeyFrameDatabase::DetectLoopCandidates(KeyFrame *pKF, float minScore) {

        set<KeyFrame *> spConnectedKeyFrames = pKF->GetConnectedKeyFrames();
        list<KeyFrame *> lKFsSharingWords;

        // Search all keyframes that share a word with current keyframes
        // Discard keyframes connected to the query keyframe
        {
            unique_lock<mutex> lock(mMutex);

            for (DBoW2::BowVector::const_iterator vit = pKF->mBowVec.begin(), vend = pKF->mBowVec.end();
                 vit != vend; vit++) {
                list<LightKeyFrame> &lKFs = mvInvertedFile[vit->first];

                for (list<LightKeyFrame>::iterator lit = lKFs.begin(), lend = lKFs.end(); lit != lend; lit++) {
                    KeyFrame *pKFi = (*lit).getKeyFrame();
                    if( pKFi ) {
                        if (pKFi->mnLoopQuery != pKF->mnId) {
                            pKFi->mnLoopWords = 0;
                            if (!spConnectedKeyFrames.count(pKFi)) {
                                pKFi->mnLoopQuery = pKF->mnId;
                                lKFsSharingWords.push_back(pKFi);
                            }
                        }
                        pKFi->mnLoopWords++;
                    }

                }
            }
        }

        if (lKFsSharingWords.empty())
            return vector<KeyFrame *>();

        list<pair<float, KeyFrame *> > lScoreAndMatch;

        // Only compare against those keyframes that share enough words
        int maxCommonWords = 0;
        for (list<KeyFrame *>::iterator lit = lKFsSharingWords.begin(), lend = lKFsSharingWords.end();
             lit != lend; lit++) {
            if ((*lit)->mnLoopWords > maxCommonWords)
                maxCommonWords = (*lit)->mnLoopWords;
        }

        int minCommonWords = maxCommonWords * 0.8f;

        int nscores = 0;

        // Compute similarity score. Retain the matches whose score is higher than minScore
        for (list<KeyFrame *>::iterator lit = lKFsSharingWords.begin(), lend = lKFsSharingWords.end();
             lit != lend; lit++) {
            KeyFrame *pKFi = *lit;

            if (pKFi->mnLoopWords > minCommonWords) {
                nscores++;

                float si = mpVoc->score(pKF->mBowVec, pKFi->mBowVec);

                pKFi->mLoopScore = si;
                if (si >= minScore)
                    lScoreAndMatch.push_back(make_pair(si, pKFi));
            }
        }

        if (lScoreAndMatch.empty())
            return vector<KeyFrame *>();

        list<pair<float, KeyFrame *> > lAccScoreAndMatch;
        float bestAccScore = minScore;

        // Lets now accumulate score by covisibility
        for (list<pair<float, KeyFrame *> >::iterator it = lScoreAndMatch.begin(), itend = lScoreAndMatch.end();
             it != itend; it++) {
            KeyFrame *pKFi = it->second;
            vector<KeyFrame *> vpNeighs = pKFi->GetBestCovisibilityKeyFrames(10);

            float bestScore = it->first;
            float accScore = it->first;
            KeyFrame *pBestKF = pKFi;
            for (vector<KeyFrame *>::iterator vit = vpNeighs.begin(), vend = vpNeighs.end(); vit != vend; vit++) {
                KeyFrame *pKF2 = *vit;
                if (pKF2->mnLoopQuery == pKF->mnId && pKF2->mnLoopWords > minCommonWords) {
                    accScore += pKF2->mLoopScore;
                    if (pKF2->mLoopScore > bestScore) {
                        pBestKF = pKF2;
                        bestScore = pKF2->mLoopScore;
                    }
                }
            }

            lAccScoreAndMatch.push_back(make_pair(accScore, pBestKF));
            if (accScore > bestAccScore)
                bestAccScore = accScore;
        }

        // Return all those keyframes with a score higher than 0.75*bestScore
        float minScoreToRetain = 0.75f * bestAccScore;

        set<KeyFrame *> spAlreadyAddedKF;
        vector<KeyFrame *> vpLoopCandidates;
        vpLoopCandidates.reserve(lAccScoreAndMatch.size());

        for (list<pair<float, KeyFrame *> >::iterator it = lAccScoreAndMatch.begin(), itend = lAccScoreAndMatch.end();
             it != itend; it++) {
            if (it->first > minScoreToRetain) {
                KeyFrame *pKFi = it->second;
                if (!spAlreadyAddedKF.count(pKFi)) {
                    vpLoopCandidates.push_back(pKFi);
                    spAlreadyAddedKF.insert(pKFi);
                }
            }
        }


        return vpLoopCandidates;
    }

    vector<long unsigned int > KeyFrameDatabase::DetectLoopCandidatesInTopoMap(KeyFrame *pKF, float minScore) {

        set<long unsigned int> spConnectedKeyFrames = pKF->getCache()->mTopoMap->GetConnectedKeyFrames( pKF->mnId );

        list<long unsigned int> lKFsSharingWords;

        // Search all keyframes that share a word with current keyframes
        // Discard keyframes connected to the query keyframe

        std::map<long unsigned int, int> mLoopWords;
        std::map<long unsigned int, double> mLoopScore;

        {
            unique_lock<mutex> lock(mMutex);

            for (DBoW2::BowVector::const_iterator vit = pKF->mBowVec.begin(), vend = pKF->mBowVec.end();
                 vit != vend; vit++) {

                list<LightKeyFrame> &lKFs = mvInvertedFile[vit->first];

                for (list<LightKeyFrame>::iterator lit = lKFs.begin(), lend = lKFs.end(); lit != lend; lit++) {

                    if( !spConnectedKeyFrames.count( (*lit).mnId ) ) {
                        if( mLoopWords.find( (*lit).mnId ) == mLoopWords.end() ){
                            mLoopWords[ (*lit).mnId ] = 0;
                            lKFsSharingWords.push_back( (*lit).mnId );
                        }

                        mLoopWords[ (*lit).mnId ] ++;

                    }

                }
            }
        }

        if (lKFsSharingWords.empty())
            return vector<long unsigned int>();

        list<pair<float, long unsigned int > > lScoreAndMatch;

        // Only compare against those keyframes that share enough words
        int maxCommonWords = 0;
        for (list<long unsigned int>::iterator lit = lKFsSharingWords.begin(), lend = lKFsSharingWords.end();
             lit != lend; lit++) {
            if ( mLoopWords[ *lit ] > maxCommonWords)
                maxCommonWords = mLoopWords[ *lit ];
        }

        int minCommonWords = maxCommonWords * 0.8f;

        int nscores = 0;

        // Compute similarity score. Retain the matches whose score is higher than minScore
        for (list<long unsigned int>::iterator lit = lKFsSharingWords.begin(), lend = lKFsSharingWords.end();
             lit != lend; lit++) {

            if (mLoopWords[ *lit ] > minCommonWords) {
                nscores++;

                float si = mpVoc->score(pKF->mBowVec, pKF->getCache()->mTopoMap->getKeyFrameBowVector( *lit ));

                mLoopScore[ *lit ] = si;

                if (si >= minScore)
                    lScoreAndMatch.push_back(make_pair(si, *lit));
            }
        }

        if (lScoreAndMatch.empty())
            return vector<long unsigned int >();

        list<pair<float, long unsigned int > > lAccScoreAndMatch;
        float bestAccScore = minScore;

        // Lets now accumulate score by covisibility
        for (list<pair<float, long unsigned int> >::iterator it = lScoreAndMatch.begin(), itend = lScoreAndMatch.end();
             it != itend; it++) {

            vector<long unsigned int > vpNeighs = pKF->getCache()->mTopoMap->GetBestCovisibilityKeyFrames((*it).second, 10);

            float bestScore = it->first;
            float accScore = it->first;
            long unsigned int pBestKF = it->second;
            for (vector<long unsigned int>::iterator vit = vpNeighs.begin(), vend = vpNeighs.end(); vit != vend; vit++) {
                long unsigned int pKF2 = *vit;
                if( mLoopWords.find(pKF2 ) != mLoopWords.end() && (mLoopScore.find(pKF2) != mLoopScore.end() )) {
                    if( mLoopWords[pKF2] > minCommonWords ) {
                        accScore += mLoopScore[ pKF2 ];
                        if ( mLoopScore[pKF2] > bestScore) {
                            pBestKF = pKF2;
                            bestScore = mLoopScore[pKF2];
                        }
                    }
                }
            }

            lAccScoreAndMatch.push_back(make_pair(accScore, pBestKF));
            if (accScore > bestAccScore)
                bestAccScore = accScore;
        }

        // Return all those keyframes with a score higher than 0.75*bestScore
        float minScoreToRetain = 0.75f * bestAccScore;

        set<long unsigned int > spAlreadyAddedKF;
        vector<long unsigned int > vpLoopCandidates;
        vpLoopCandidates.reserve(lAccScoreAndMatch.size());

        for (list<pair<float, long unsigned int > >::iterator it = lAccScoreAndMatch.begin(), itend = lAccScoreAndMatch.end();
             it != itend; it++) {
            if (it->first > minScoreToRetain) {
                long unsigned int pKFi = it->second;
                if (!spAlreadyAddedKF.count(pKFi)) {
                    vpLoopCandidates.push_back(pKFi);
                    spAlreadyAddedKF.insert(pKFi);
                }
            }
        }

        return vpLoopCandidates;
    }

    vector<KeyFrame *> KeyFrameDatabase::DetectRelocalizationCandidates(Frame *F) {
        list<KeyFrame *> lKFsSharingWords;

        // Search all keyframes that share a word with current frame
        {
            unique_lock<mutex> lock(mMutex);

            for (DBoW2::BowVector::const_iterator vit = F->mBowVec.begin(), vend = F->mBowVec.end();
                 vit != vend; vit++) {
                list<LightKeyFrame> &lKFs = mvInvertedFile[vit->first];

                for (list<LightKeyFrame>::iterator lit = lKFs.begin(), lend = lKFs.end(); lit != lend; lit++) {
                    KeyFrame *pKFi = (*lit).getKeyFrame();
                    if( pKFi ) {
                        if (pKFi->mnRelocQuery != F->mnId) {
                            pKFi->mnRelocWords = 0;
                            pKFi->mnRelocQuery = F->mnId;
                            lKFsSharingWords.push_back(pKFi);
                        }
                        pKFi->mnRelocWords++;
                    }
                }
            }
        }
        if (lKFsSharingWords.empty())
            return vector<KeyFrame *>();

        // Only compare against those keyframes that share enough words
        int maxCommonWords = 0;
        for (list<KeyFrame *>::iterator lit = lKFsSharingWords.begin(), lend = lKFsSharingWords.end();
             lit != lend; lit++) {
            if ((*lit)->mnRelocWords > maxCommonWords)
                maxCommonWords = (*lit)->mnRelocWords;
        }

        int minCommonWords = maxCommonWords * 0.8f;

        list<pair<float, KeyFrame *> > lScoreAndMatch;

        int nscores = 0;

        // Compute similarity score.
        for (list<KeyFrame *>::iterator lit = lKFsSharingWords.begin(), lend = lKFsSharingWords.end();
             lit != lend; lit++) {
            KeyFrame *pKFi = *lit;

            if (pKFi->mnRelocWords > minCommonWords) {
                nscores++;
                float si = mpVoc->score(F->mBowVec, pKFi->mBowVec);
                pKFi->mRelocScore = si;
                lScoreAndMatch.push_back(make_pair(si, pKFi));
            }
        }

        if (lScoreAndMatch.empty())
            return vector<KeyFrame *>();

        list<pair<float, KeyFrame *> > lAccScoreAndMatch;
        float bestAccScore = 0;

        // Lets now accumulate score by covisibility
        for (list<pair<float, KeyFrame *> >::iterator it = lScoreAndMatch.begin(), itend = lScoreAndMatch.end();
             it != itend; it++) {
            KeyFrame *pKFi = it->second;
            vector<KeyFrame *> vpNeighs = pKFi->GetBestCovisibilityKeyFrames(10);

            float bestScore = it->first;
            float accScore = bestScore;
            KeyFrame *pBestKF = pKFi;
            for (vector<KeyFrame *>::iterator vit = vpNeighs.begin(), vend = vpNeighs.end(); vit != vend; vit++) {
                KeyFrame *pKF2 = *vit;
                if (pKF2->mnRelocQuery != F->mnId)
                    continue;

                accScore += pKF2->mRelocScore;
                if (pKF2->mRelocScore > bestScore) {
                    pBestKF = pKF2;
                    bestScore = pKF2->mRelocScore;
                }

            }
            lAccScoreAndMatch.push_back(make_pair(accScore, pBestKF));
            if (accScore > bestAccScore)
                bestAccScore = accScore;
        }

        // Return all those keyframes with a score higher than 0.75*bestScore
        float minScoreToRetain = 0.75f * bestAccScore;
        set<KeyFrame *> spAlreadyAddedKF;
        vector<KeyFrame *> vpRelocCandidates;
        vpRelocCandidates.reserve(lAccScoreAndMatch.size());
        for (list<pair<float, KeyFrame *> >::iterator it = lAccScoreAndMatch.begin(), itend = lAccScoreAndMatch.end();
             it != itend; it++) {
            const float &si = it->first;
            if (si > minScoreToRetain) {
                KeyFrame *pKFi = it->second;
                if (!spAlreadyAddedKF.count(pKFi)) {
                    vpRelocCandidates.push_back(pKFi);
                    spAlreadyAddedKF.insert(pKFi);
                }
            }
        }

        return vpRelocCandidates;
    }

} //namespace ORB_SLAM
