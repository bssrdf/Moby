#!/bin/bash

# simple script for rendering a movie that takes in static camera positions
# and targets

usage()
{
  echo "render_movie_simple.sh <path to Moby render> <path to osg files/output> <camera position> <camera target> <movie file>"
  echo " -- sample camera position: 0 5 10"
  echo " -- sample camera target: 0 0 0"
  exit 
}

main ()
{
  # ffmpeg requires particular numbering
  a=1
  for i in $2/driver.out-*.osg; do
  new=$(printf "img%04d.png" ${a});
  let a=a+1;
  #render-osg $i -p 0.55 -0.75 0.5 -t $(awk 'NR == n' n=${a} $1/com.mat) -s=$1/scene.osg $1/${new};
echo $1/moby-render $i -p $3 -t $4 $2/${new} 
  $1/moby-render $i -p $3 $4 $5 -t $6 $7 $8 $2/${new};
done

# number all images img0001.png, etc...
ffmpeg -r 25 -i $1/img%04d.png -f mp4 -q:v 0 -vcodec mpeg4 $9
}


# check for proper number of arguments
[ "$#" -ne 9 ] && ( usage && exit) || main $1 $2 $3 $4 $5 $6 $7 $8 $9


