/*
 Copyright 2008, 2009, 2010 Damian Stewart <damian@frey.co.nz>.
 
 This file is part of The Artvertiser.
 
 The Artvertiser is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 The Artvertiser is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public License
 along with The Artvertiser.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "DProfiler.h"

#include <algorithm>

#include "DThread.h"

DProfiler::DProfileContexts DProfiler::contexts;
DSemaphore DProfiler::lock;

int DProfileSection::EXEC_ORDER_ID = 0;

DTime DProfiler::end_time;

DProfileContext::~DProfileContext()
{
    if ( toplevel )
        delete toplevel;
}

DProfileContext* DProfiler::GetContext()
{
	DThreadContext thread_context;
	lock.Wait();
	// try to get current thread context
	for ( DProfileContexts::const_iterator i = contexts.begin();
		  i!= contexts.end();
		  ++i )
	{
		if ( thread_context == ( *i )->thread_context )
		{
			lock.Signal();
			return *i;
		}
	}

	// no context found for this thread: must create a new one
	DProfileContext* context = new DProfileContext();
	// add it to the vector
	contexts.push_back( context );
	// fill in details
	context->thread_context.Set();
	context->toplevel = new DProfileSection();
	context->current = context->toplevel;

	// return
	lock.Signal();
	return context;
}



void DProfiler::Clear()
{
    // get lock
    lock.Wait();
    // delete everything
    for ( int i=0; i<contexts.size(); i++ )
    {
        delete contexts[i];
    }
    contexts.clear();

    // done
    lock.Signal();

}

void DProfiler::SectionPush(const std::string &name)
{
	DProfileContext* context = GetContext();
	assert( context->current );

	// try to grab the section out of the db
	// we store by name so that we can accumulate results over multiple frames
	DProfileSection* s = context->current->children[name];
	if ( s == NULL )
	{
		s = new DProfileSection();
		s->parent = context->current;
		s->name = name;
		context->current->children[name] = s;
	}

	// shift current to us
	context->current = s;

	// store start time
	context->current->timer.SetNow();
//	QueryPerformanceCounter(&s->start_time);

}


void DProfiler::SectionPop()
{
    end_time.SetNow();

	// grab the section
	DProfileContext* context = GetContext();
	DProfileSection* s = context->current;

    // check we're not popping up too far
	if ( context->current->parent == NULL )
        return;

	// get time for this run in ms
/*	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	double time = 1000*(double)(stop_time.QuadPart - s->start_time.QuadPart)/(double)freq.QuadPart;
	*/
    double time = (end_time-s->timer).ToMillis();
    //double time = end_time.ToMillis()-s->timer.ToMillis();


	// work out the new avg time and increment the call count
	double total_time = time + s->avg_time * s->call_count++;
	s->avg_time = total_time/(double)s->call_count;
	/*s->avg_time = (s->avg_time*7 + time)/8;
	s->call_count = 8;*/

	// shift current up
	context->current = context->current->parent;
}

void DProfiler::Display( DProfiler::SORT_BY sort )
{
	printf("---------------------------------------------------------------------------------------\n" );
    // re-use formatting from individual lines
    printf( "PRofiler output: sorted by %s\n", (sort==SORT_EXECUTION?"execution order":"total time"));
    printf( "%-50s  %10s  %10s  %6s\n", "name                            values in ms -> ", "total ", "average ", "count" );
    printf("---------------------------------------------------------------------------------------\n" );
	lock.Wait();
	for ( DProfileContexts::iterator i = contexts.begin();
		  i != contexts.end();
		  ++i )
	{
		printf("Thread %x\n", (unsigned long)&((*i)->thread_context) );
		(*i)->toplevel->Display("| ", sort );
	}
	lock.Signal();
	printf("---------------------------------------------------------------------------------------\n" );
}


DProfileSection::DProfileSection()
{
	parent = NULL;
	avg_time = 0;
	call_count = 0;
	exec_order_id = EXEC_ORDER_ID++;
}

DProfileSection::~DProfileSection()
{
    for ( DProfileSections::iterator i = children.begin();
        i != children.end();
        ++i )
    {
        delete (*i).second;
    }
    children.clear();
}

bool reverse_time_comparator( DProfileSection* a, DProfileSection* b )
{
    return a->avg_time*a->call_count > b->avg_time*b->call_count;
}

bool execution_order_comparator( DProfileSection* a, DProfileSection* b )
{
    return a->exec_order_id < b->exec_order_id;
}

void DProfileSection::Display( const std::string& prefix, DProfiler::SORT_BY sort_by )
{
    std::vector<DProfileSection* > children_vect;
	for ( DProfileSections::iterator i = children.begin();
		  i!= children.end();
		  ++i )
	{
        children_vect.push_back( (*i).second );
	}

    // sort by ..
    if ( sort_by == DProfiler::SORT_TIME )
    {
        std::sort( children_vect.begin(), children_vect.end(), reverse_time_comparator );
    }
    else if ( sort_by == DProfiler::SORT_EXECUTION )
    {
        std::sort( children_vect.begin(), children_vect.end(), execution_order_comparator );
    }

    for ( int i=0; i<children_vect.size(); i++ )
    {
        DProfileSection* sect = children_vect[i];
	    // replace '+' with '|';
		std::string name;
		if ( prefix.size()>1 )
            name = prefix.substr( 0, prefix.size()-2 ) + std::string("+ ") + sect->name;
        else
            name = sect->name;
		printf( "%-50s  %10.2f  %10.5f  %6d\n", name.c_str(),
				  sect->avg_time * sect->call_count,
				  sect->avg_time, sect->call_count );

        // if this is the last child,
        std::string next_prefix = prefix;
        if ( prefix.size() > 1 && i==children_vect.size()-1 )
        {
            // erase the previous "| " and replace with "  "
            next_prefix = next_prefix.substr(0, next_prefix.size()-2 ) + std::string("  ");
        }
        // next deeper level
        sect->Display( next_prefix + "| ", sort_by );

	}
}
