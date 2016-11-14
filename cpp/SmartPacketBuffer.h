/*
 * This file is protected by Copyright. Please refer to the COPYRIGHT file
 * distributed with this source distribution.
 *
 * This file is part of REDHAWK rh.SourceSDDS.
 *
 * REDHAWK rh.SourceSDDS is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * REDHAWK rh.SourceSDDS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */
/*
 * SmartPacketBuffer.h
 *
 *  Created on: Mar 29, 2016
 *      Author: 
 */

#ifndef SMARTPACKETBUFFER_H_
#define SMARTPACKETBUFFER_H_

#include <boost/circular_buffer.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/thread.hpp>
#include <boost/call_traits.hpp>
#include <string>
#include <stdio.h>
#include <iostream>
#include <deque>


/**
 * Two deques of boost smart pointers
 * One deque full of empty buffers to be used
 * One deque where filled buffers are placed.
 * You MUST follow this cycle: pop_empty_buffer -> push_full_buffer -> pop_full_buffer -> recycle_buffer
 * Memory is only allocated at construction so if you do not follow the above cycle and a smart pointer goes
 * out of scope it will free itself.
 *
 * The locking here is done with conditional variables so that it should be quick however if this ends up being
 * a point of thread contention it could be reimplemented using a no-wait no-locking queue. I've tested it at
 * 3Gbps so I do not think that will become an issue but see this post if someone in the future wants to go
 * down that path: http://www.drdobbs.com/parallel/writing-lock-free-code-a-corrected-queue/210604448?pgno=2
 * There are better options than drdobbs example however most require C++11 and his example only requires c++0x
 * There is no license or copyright shown however after emailing him regarding use he responded with
 * "Hi Youssef, it’s “as is” for you to use. Thanks,"
 *
 */
template <class T >
class SmartPacketBuffer {
public:

	typedef boost::shared_ptr<T> TypePtr;
    typedef std::deque<TypePtr> container_type;
    typedef typename container_type::size_type size_type;
    typedef typename container_type::value_type value_type;

    explicit SmartPacketBuffer():m_shuttingDown(false) {}

    /**
     * Initializes the empty buffers container with capacity
     * new Boost shared pointers of the templated type provided.
     * If this Smart Packet Buffer was previously initialized,
     * one should call destroy buffers before the call to initialize.
     * Only the empty buffer container is cleared prior to initialization.
     *
     * @param capacity The size of the emtpy buffers container after initialization
     */
    void initialize(size_type capacity) {
    	boost::unique_lock<boost::mutex> lock(m_empty_buffer_mutex);
		m_shuttingDown = false;
    	m_empty_buffers.clear();

    	// Allocate the memory and fill the empty buffers
    	for (size_t i = 0; i < capacity; ++i) {
    		m_empty_buffers.push_back(TypePtr(new T()));
    	}
    	lock.unlock();
    }

    /**
     * Notifies any waiting thread that the packet buffer is shutting down and
     * frees all the memory that the smart buffer currently is keeping track of.
     * This does not include any buffers currently held by other threads which
     * would need to be recycled or returned prior to calling destroyBuffers.
     * There is no harm in calling shutDown more than once if one needs
     * to free the thread holding the data to recycle it.
     *
     * After a call to shutDown, you will need to call initialize again before
     * using this class
     */
    void shutDown() {
    	m_shuttingDown = true;
    	m_no_empty_buffers.notify_all();
    	m_no_full_buffers.notify_all();

		boost::unique_lock<boost::mutex> lock1(m_full_buffer_mutex);
    	m_full_buffers.clear();
    	lock1.unlock();

    	boost::unique_lock<boost::mutex> lock2(m_empty_buffer_mutex);
    	m_empty_buffers.clear();
		lock2.unlock();
    }


    /**
     * Pops a single empty buffer off of the empty buffer container. Will block
     * if no empty buffers are available.
     * NOTE: Not as well tested as pop_empty_buffers but included for completness.
     */
    TypePtr pop_empty_buffer() {
    	if (m_shuttingDown) {return NULL;}
    	boost::unique_lock<boost::mutex> lock(m_empty_buffer_mutex);
    	m_no_empty_buffers.wait(lock, boost::bind(&SmartPacketBuffer<T>::empties_available, this));
    	if (m_shuttingDown) {return NULL;}
    	TypePtr retVal = *m_empty_buffers.begin();
    	m_empty_buffers.pop_front();
    	lock.unlock();
    	return retVal;
    }

    /**
     * Fill the provided container until it is len in size of empty buffers.
     * Will block until the request can be satisified (ie. there are len buffers available)
     */
    template<typename Container>
    void pop_empty_buffers(Container &que, size_t len) {
    		if (m_shuttingDown) {return;}

    		// Maybe they have what they want already
        	if (que.size() >= len)
        		return;

        	size_t request = len - que.size();

        	boost::unique_lock<boost::mutex> lock(m_empty_buffer_mutex);
        	m_no_empty_buffers.wait(lock, boost::bind(&SmartPacketBuffer<T>::empties_available, this, request));
        	if (m_shuttingDown) {return;}

        	// Really wish we could use c++11 and just use move :-p
        	// Or more boost::move but that is 1.49
        	que.insert(que.begin(), m_empty_buffers.begin(), m_empty_buffers.begin() + request);
        	m_empty_buffers.erase(m_empty_buffers.begin(), m_empty_buffers.begin() + request);

        	lock.unlock();
        }

    /**
     * Pushes a single full buffer on to the full buffer container. Will block
     * if anther thread has the full buffer container lock.
     * NOTE: Not as well tested as push_full_buffers but included for completness.
     */
    void push_full_buffer(TypePtr b) {
    	if (m_shuttingDown) {return;}
    	boost::unique_lock<boost::mutex> lock(m_full_buffer_mutex);
    	m_full_buffers.push_back(b);
    	lock.unlock();
    	m_no_full_buffers.notify_one();
    }

    /**
     * Pushes all the buffers contained in provided container onto the internal full buffer container
     * and clears the povided container. Will block if another thread has the full buffer lock.
     */
    template<typename Container>
    void push_full_buffers(Container &que, size_t num) {
    	if (m_shuttingDown) {
    		que.erase(que.begin(), que.begin() + num);
    		return;
    	}

    	boost::unique_lock<boost::mutex> lock(m_full_buffer_mutex);
		m_full_buffers.insert(m_full_buffers.end(), que.begin(), que.begin() + num);
		que.erase(que.begin(), que.begin() + num);
    	lock.unlock();
		m_no_full_buffers.notify_one();
    }

    /**
     * Returns a single full buffer. Will block if a full buffer is not available.
     * NOTE: Not as well tested as pop_full_buffers but included for completness.
     */
    TypePtr pop_full_buffer() {
    	if (m_shuttingDown) {return NULL;}
    	boost::unique_lock<boost::mutex> lock(m_full_buffer_mutex);
		m_no_full_buffers.wait(lock, boost::bind(&SmartPacketBuffer<T>::full_available, this));
		if (m_shuttingDown) {return NULL;}
		TypePtr retVal = *m_full_buffers.begin();
		m_full_buffers.pop_front();
		lock.unlock();
		return retVal;
	}


    /**
     * Fill the provided container until it is len in size of full buffers.
     * Will block until len buffers are available.
     */
    template<typename Container>
    void pop_full_buffers(Container &que, size_t len) {
    	if (m_shuttingDown) {return;}
		// Maybe they have what they want already
    	if (que.size() >= len)
    		return;

    	size_t request = len - que.size();

    	boost::unique_lock<boost::mutex> lock(m_full_buffer_mutex);
		m_no_full_buffers.wait(lock, boost::bind(&SmartPacketBuffer<T>::full_available, this, request));
		if (m_shuttingDown) {return;}

		que.insert(que.end(), m_full_buffers.begin(), m_full_buffers.begin() + request);
		m_full_buffers.erase(m_full_buffers.begin(), m_full_buffers.begin() + request);

		lock.unlock();
	}

    /**
     * Returns a single buffer to the internal empty buffer container.
     * Will block if a nother thread holds the empty buffer lock.
     * NOTE: Not as well tested as recycle_buffers but included for completness.
     */
    void recycle_buffer(TypePtr b) {
    	if (m_shuttingDown) {return;}
    	boost::unique_lock<boost::mutex> lock(m_empty_buffer_mutex);
    	m_empty_buffers.push_back(b);
    	lock.unlock();
    	m_no_empty_buffers.notify_one();
    }

    /**
     * Returns all buffers in provided que container to the internal empty buffer container.
     * Will block if another thread holds the empty buffer lock.
     */
    template<typename Container>
    void recycle_buffers(Container &que) {
    	if (m_shuttingDown) {
    		que.clear();
    		return;
    	}

    	boost::unique_lock<boost::mutex> lock(m_empty_buffer_mutex);
    	m_empty_buffers.insert(m_empty_buffers.end(), que.begin(), que.end());
    	que.clear();
    	lock.unlock();
    	m_no_empty_buffers.notify_one();
    }

    /**
     * Returns the number of buffers in the internal full buffers container.
     */
    size_t get_num_full_buffers() {
    	return m_full_buffers.size();
    }

    /**
     * Returns the number of buffers in the internal empty buffers container.
     */
    size_t get_num_empty_buffers() {
    	return m_empty_buffers.size();
    }

private:
    SmartPacketBuffer(const SmartPacketBuffer&);              // Disabled copy constructor
    SmartPacketBuffer& operator = (const SmartPacketBuffer&); // Disabled assign operator
    bool m_shuttingDown;

    /**
     * If we are shutting down we need to just open the gates up and let the threads run.
     * If not we'll have folks blocking on us
     */
    bool empties_available() const { return m_empty_buffers.size() > 0 					|| m_shuttingDown; }
    bool empties_available(size_t num) const { return m_empty_buffers.size() >= num 	|| m_shuttingDown; }
    bool full_available() const { return m_full_buffers.size() > 0 						|| m_shuttingDown; }
    bool full_available(size_t num) const { return m_full_buffers.size() >= num 		|| m_shuttingDown; }


    container_type m_empty_buffers;
    container_type m_full_buffers;
    boost::mutex m_empty_buffer_mutex;
    boost::mutex m_full_buffer_mutex;
    boost::condition_variable m_no_empty_buffers;
    boost::condition_variable m_no_full_buffers;
};

#endif /* PACKETBUFFER_H_ */
