-- this is an experiment in grouping
-- regroup2: keep track of the permutations...

module Main where

import List
import IO
import System
import Debug.Trace
import Array

delIndex i l = 
  let (xs,y:ys) = splitAt i l
  in xs ++ ys

addIndex x i l = 
  let (xs,ys) = splitAt i l
  in xs ++ x:ys
 
swap_row i j l = 
  let x = l !! i
  in addIndex x j (delIndex i l)

fst3 (x,_,_) = x
snd3 (_,y,_) = y
trd3 (_,_,z) = z

cost_row l = length $ dropWhile (=='-') $ reverse $ dropWhile (=='-') $ map fst3 l

cost g     = sum (map cost_row g)
swap g i j = map (swap_row i j) g

costswap g i j 
--  = cost (swap g i j)
--  = sum (map cost_row (map (swap_row i j) g))
--  = foldl + 0 (map (cost_row . swap_row i j) g)
    = foldl (\y -> \x -> cost_row (swap_row i j x) + y) 0 g
 
allswaps l = (0,0,cost l) : 
  [ (i,j,k) | 
      let n=length (head l) -1,
      i <- [0..n], 
      j <- [0..n], 
      j<i || j>i+1,
      let k=costswap l i j]

lesscost (a,b,c) (d,e,f) = compare c f

-- this doesn't take the _best_ swap, but the first that leads to a decrease.
-- drp c (_,_,k) = c<=k
-- firstswap g = head (dropWhile (drp (cost g)) (allswaps g) ++ [(0,0,cost g)])

bestswap g = minimumBy lesscost (allswaps g)

nextswap g = let (i,j,_) = bestswap g in  (trace (show i ++ " -> " ++ show j)) 
                    swap g i j

optimal_group g =
  let h = nextswap g in
  if cost g==cost h then g else optimal_group h

-----------------------------------------------------------------------
allinsert x [] = [[x]]
allinsert x (y:l) = (x:y:l) : map (y:) (allinsert x l)

allperms [] = [[]]
allperms (x:l) = concat (map (allinsert x) (allperms l))

applypermrow [] _ = []
applypermrow (m:l) r = 
   let (xs,y:ys) = (splitAt m r)
   in y : applypermrow l r

applyperm g p = map (applypermrow p) g
lesscostperm (k,c) (l,f) = compare c f

bestresult g =
  let l = length $ head g
      ps = allperms [0..l-1]
      gs = map (applyperm g) ps
      (i,j) = minimumBy lesscostperm $ map (\(i,g)->(i,cost g)) (zip [0..] gs)
  in applyperm g (allperms [0..l-1] !! i)

-----------------------------------------------------------------------

cmp (a,_,_) (b,_,_) = compare a b
cmp_row [] [] = EQ
cmp_row (x:l) (y:k) =
   case cmp x y of
     GT -> GT
     LT -> LT
     EQ -> cmp_row l k

sort_matrix g = sortBy cmp_row g

merge_rows [] [] = []
merge_rows ((a,b,c):l) ((d,e,f):k) = (a,b++e,c) : merge_rows l k

nub_matrix [] = []
nub_matrix [l] = [l]
nub_matrix (k:l:x) = 
  case cmp_row k l of
    EQ -> nub_matrix (merge_rows k l : x)
    _  -> k:nub_matrix (l:x)

-----------------------------------------------------------------

map2 _ [] = []
map2 n (x:l) = (n,x): map2 (n+1) l

mapping l = map2 0 l

state_mapping g = mapping (map trd3 (head g))
trans_mapping g = mapping (map (snd3 . head) g)

dep3 _ [] = []
dep3 c (('-',_,_):l) = dep3 (c+1) l
dep3 c (('+',_,_):l) = c : dep3 (c+1) l

dep2 r [] = []
dep2 r (l:g) = (r,dep3 0 l) : dep2 (r+1) g

dependencies i = dep2 0 i
-----------------------------------------------------------------

print_row fp []    = hPutStr fp "\n"
print_row fp ((c,_,_):l) = do hPutStr fp [c] ; print_row fp l

pprint _ []    = return ()
pprint fp (l:g) = do print_row fp l; pprint fp g

convert r c [] = []
convert r c (x:l) = (x,[r],[c]):convert r (c+1) l

readmatrix r g = do               
  b <- isEOF
  if b then return g else do
    l <- getLine
    readmatrix (r+1) (convert r 0 l : g)

stats s g =
  hPutStr stderr (s ++ ": " 
     ++ show (length g) ++ " rows, " 
     ++ show (length (head g)) ++ " columns, penalty: " 
     ++ show (cost g) ++ "\n")

main = do 
  l <- getArgs
  g <- readmatrix 0 []
  let h = nub_matrix $ sort_matrix g

  i <- if (elem "-sort" l) then do
     return h
   else if (elem "-best" l) then do 
     hPutStr stderr "All permutations:\n"
     let i = sort_matrix $ bestresult h
     return i
   else do
     hPutStr stderr "Shifting columns:\n"
     let i = sort_matrix $ optimal_group h
     return i

  i `seq` stats "Input  matrix" g
  stats "Sorted matrix" h
  stats "Output matrix" i

  if (elem "-matrix" l)
   then do 
     hPutStr stderr "Resulting Matrix:\n"
     pprint stdout i
   else do
     hPutStr stderr "Writing state mapping, transition mapping, dependencies to stdout:\n"
     print (state_mapping i)
     print (trans_mapping i)
     print (dependencies i)
